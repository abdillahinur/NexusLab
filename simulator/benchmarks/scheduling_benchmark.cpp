// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/scheduling/policy.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/workload/run.hpp"
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace nexuslab {
namespace {
struct Options final {
    std::string policy{"first-fit"};
    std::string mode{"burst"};
    std::size_t gpus{128};
};
Options parse(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            throw std::invalid_argument{"expected option value"};
        }
        const std::string_view key{argv[i]};
        const std::string_view value{argv[i + 1]};
        if (key == "--policy") {
            result.policy = value;
        } else if (key == "--case") {
            result.mode = value;
        } else if (key == "--gpus") {
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), result.gpus);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                throw std::invalid_argument{"invalid GPU count"};
            }
        } else {
            throw std::invalid_argument{"unknown benchmark option"};
        }
    }
    if (result.gpus < 64 || result.gpus > 8192 || result.gpus % 64 != 0) {
        throw std::invalid_argument{"invalid Clos size"};
    }
    static_cast<void>(scheduling::make_policy(result.policy, 42));
    return result;
}
std::uint64_t rss() {
    std::ifstream input{"/proc/self/status"};
    std::string key;
    while (input >> key) {
        if (key == "VmHWM:") {
            std::uint64_t value{0};
            input >> value;
            return value;
        }
        std::string rest;
        std::getline(input, rest);
    }
    throw std::runtime_error{"Linux RSS unavailable"};
}
void hash(std::uint64_t& digest, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        digest ^= (value >> shift) & 255U;
        digest *= 1099511628211ULL;
    }
}
workload::TrainingScenario scenario(const Options& options) {
    workload::TrainingScenario result;
    result.gpus = options.gpus;
    result.scheduling = scheduling::Configuration{options.policy, 42};
    if (options.mode != "sequential" && options.mode != "burst" && options.mode != "mixed" &&
        options.mode != "failure" && options.mode != "rack-pressure") {
        throw std::invalid_argument{"unknown workload case"};
    }
    if (options.mode == "rack-pressure") {
        workload::JobSpec holder;
        holder.name = "rack-pressure-holder";
        for (std::uint64_t i = 0; i < 60; ++i) {
            holder.workers.emplace_back(i);
            holder.compute.emplace_back(100000);
        }
        holder.gradient_bytes = transport::ByteCount{64};
        holder.bucket_bytes = transport::ByteCount{64};
        result.jobs.push_back(std::move(holder));
    }
    for (std::uint32_t i = 0; i < 16; ++i) {
        workload::JobSpec spec;
        spec.name = "placement-comparison";
        spec.requested_workers = options.mode == "mixed" ? 4U * (1 + i % 8) : 16;
        if (options.mode == "rack-pressure") {
            spec.requested_workers = 8;
        }
        spec.compute.assign(spec.requested_workers, sim::SimDurationNs{10000});
        spec.arrival = sim::SimTimeNs{options.mode == "sequential" ? i * 20000ULL : 1ULL};
        spec.priority = i % 3;
        spec.steps = 2;
        result.jobs.push_back(std::move(spec));
    }
    if (options.mode == "failure") {
        result.gpu_controls = {{topology::GpuId{0}, false, sim::SimTimeNs{5000}},
                               {topology::GpuId{0}, true, sim::SimTimeNs{20000}}};
    }
    return result;
}
void planning(const Options& options) {
    auto graph = topology::generate_clos({options.gpus, 8, 8, 8});
    scheduling::ResourceInventory inventory{*graph};
    auto policy = scheduling::make_policy(options.policy, 42);
    std::uint64_t digest{14695981039346656037ULL};
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < 1000; ++i) {
        const auto result = policy->place({workload::JobId{i}, 32, 0, {}}, inventory.view());
        if (result.workers.size() != 32) {
            throw std::logic_error{"planner failed available request"};
        }
        for (const auto gpu : result.workers) {
            hash(digest, gpu.value());
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::cout << "planning_calls=1000\nelapsed_ns=" << elapsed << "\ndomain_digest=" << digest
              << '\n';
}
void training(const Options& options) {
    const auto input = scenario(options);
    const auto start = std::chrono::steady_clock::now();
    const auto report = workload::run_training(input);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::uint64_t digest{14695981039346656037ULL};
    std::uint64_t succeeded{0};
    std::uint64_t failed{0};
    std::uint64_t wait{0};
    std::uint64_t idle{0};
    std::uint64_t racks{0};
    std::uint64_t nics{0};
    std::uint64_t cross{0};
    std::uint64_t local{0};
    std::uint64_t fabric{0};
    std::uint64_t delivered{0};

    for (const auto& job : report.jobs) {
        if (!workload::terminal(job.state)) {
            throw std::logic_error{"benchmark left waiting job"};
        }
        if (job.state == workload::JobState::Succeeded) {
            ++succeeded;
        } else {
            ++failed;
        }
        wait = workload::checked_sum(wait, job.waiting_ns);
        idle = workload::checked_sum(idle, job.idle_gpu_ns);
        hash(digest, job.id.value());
        hash(digest, static_cast<std::uint64_t>(job.state));
        hash(digest, job.elapsed_ns);
        hash(digest, job.waiting_ns);
        hash(digest, job.compute_gpu_ns);
        hash(digest, job.idle_gpu_ns);
    }
    for (const auto& placement : report.placements) {
        hash(digest, placement.job.value());
        hash(digest, placement.timestamp.count());
        hash(digest, static_cast<std::uint64_t>(placement.outcome));
        hash(digest, placement.fragmentation_before);
        hash(digest, placement.fragmentation_after);
        racks += placement.locality.racks;
        nics += placement.locality.nics;
        cross += placement.locality.cross_rack_ring_edges;
        for (const auto gpu : placement.workers) {
            hash(digest, gpu.value());
        }
    }
    for (const auto& collective : report.collectives) {
        local += collective.issued_local_bytes;
        fabric += collective.issued_fabric_bytes;
        delivered += collective.delivered_bytes;
        hash(digest, static_cast<std::uint64_t>(collective.outcome));
        hash(digest, collective.delivered_bytes);
    }
    for (const auto& decision : report.decisions) {
        for (const auto& hop : decision.path) {
            hash(digest, hop.link.value());
            hash(digest, static_cast<std::uint64_t>(hop.direction));
        }
    }
    if (options.mode != "failure" && failed != 0) {
        throw std::logic_error{"unexpected job failure in placement comparison"};
    }
    std::cout << "elapsed_ns=" << elapsed << "\njobs=" << report.jobs.size()
              << "\nsucceeded_jobs=" << succeeded << "\nfailed_jobs=" << failed
              << "\nfinal_time_ns=" << report.simulation.final_time.count()
              << "\nevents=" << report.simulation.dispatched_events
              << "\nscheduling_wait_ns=" << wait << "\nidle_gpu_ns=" << idle
              << "\nplacement_decisions=" << report.placements.size()
              << "\nallocated_racks_sum=" << racks << "\nallocated_nics_sum=" << nics
              << "\ncross_rack_ring_edges_sum=" << cross << "\nlocal_bytes=" << local
              << "\nfabric_bytes=" << fabric << "\ndelivered_bytes=" << delivered
              << "\nroute_decisions=" << report.decisions.size() << "\ndomain_digest=" << digest
              << '\n';
}
} // namespace
} // namespace nexuslab
int main(int argc, char** argv) {
    try {
        const auto options = nexuslab::parse(argc, argv);
        std::cout << "benchmark=scheduling\nsynthetic=true\npolicy=" << options.policy
                  << "\ncase=" << options.mode << "\ngpus=" << options.gpus << "\nseed=42\n";
        if (options.mode == "planning") {
            nexuslab::planning(options);
        } else {
            nexuslab::training(options);
        }
        std::cout << "peak_rss_kib=" << nexuslab::rss()
                  << "\nevent_size_bytes=" << sizeof(nexuslab::sim::Event) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
