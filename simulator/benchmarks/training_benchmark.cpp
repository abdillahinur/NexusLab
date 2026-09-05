// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/workload/run.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
namespace {
namespace workload = nexuslab::workload;
namespace collective = nexuslab::collective;
namespace transport = nexuslab::transport;
namespace topology = nexuslab::topology;
namespace sim = nexuslab::sim;
struct Options final {
    std::string mode{"training"};
    std::uint32_t workers{8};
    std::uint32_t jobs{1};
    std::uint32_t steps{3};
    std::uint64_t gradient{65'536};
    std::uint64_t bucket{65'536};
    std::uint64_t chunk{4'096};
    std::uint64_t compute{10'000};
    std::uint64_t buffer{262'144};
    bool overlap{false};
    bool shared_nics{false};
};
std::uint64_t number(std::string_view text) {
    std::uint64_t value{0};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument{"invalid benchmark number"};
    }
    return value;
}
void set_numeric_option(Options& result, std::string_view key, std::uint64_t n) {
    if (n > 1'000'000'000ULL) {
        throw std::length_error{"benchmark value too large"};
    }
    if (key == "--workers") {
        result.workers = static_cast<std::uint32_t>(n);
    } else if (key == "--jobs") {
        result.jobs = static_cast<std::uint32_t>(n);
    } else if (key == "--steps") {
        result.steps = static_cast<std::uint32_t>(n);
    } else if (key == "--gradient-bytes") {
        result.gradient = n;
    } else if (key == "--bucket-bytes") {
        result.bucket = n;
    } else if (key == "--chunk-bytes") {
        result.chunk = n;
    } else if (key == "--compute-ns") {
        result.compute = n;
    } else if (key == "--buffer-bytes") {
        result.buffer = n;
    } else if (key == "--shared-nics" && n <= 1) {
        result.shared_nics = n != 0;
    } else if (key == "--overlap" && n <= 1) {
        result.overlap = n != 0;
    } else {
        throw std::invalid_argument{"unknown benchmark option"};
    }
}
Options parse(int argc, char** argv) {
    Options result;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            throw std::invalid_argument{"expected option value"};
        }
        const std::string_view key{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (key == "--mode") {
            result.mode = value;
            continue;
        }
        set_numeric_option(result, key, number(value));
    }
    if ((result.mode != "training" && result.mode != "planning") || result.workers == 0 ||
        result.workers > 8192 || result.jobs == 0 || result.jobs > 16 || result.steps == 0 ||
        result.steps > 100 || result.gradient == 0 || result.bucket == 0 || result.chunk == 0 ||
        result.compute == 0) {
        throw std::invalid_argument{"invalid benchmark bounds"};
    }
    if (result.mode == "training" &&
        (result.workers > 128 || result.jobs * result.workers > 1024)) {
        throw std::length_error{"training benchmark scale exceeds retained-state budget"};
    }
    if (result.shared_nics && result.jobs > 8) {
        throw std::invalid_argument{"shared NIC mode allows at most eight jobs"};
    }
    return result;
}
std::uint64_t peak_rss() {
    std::ifstream input{"/proc/self/status"};
    std::string key;
    while (input >> key) {
        if (key == "VmHWM:") {
            std::uint64_t value{0};
            std::string unit;
            if (input >> value >> unit && unit == "kB") {
                return value;
            }
            break;
        }
        input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    throw std::runtime_error{"Linux RSS unavailable"};
}
void hash_field(std::uint64_t& hash, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= (value >> shift) & 255U;
        hash *= 1099511628211ULL;
    }
}
std::uint64_t digest(const workload::TrainingReport& report) {
    std::uint64_t hash{14695981039346656037ULL};
    for (const auto& job : report.jobs) {
        for (const auto value : {job.id.value(), static_cast<std::uint64_t>(job.state),
                                 static_cast<std::uint64_t>(job.completed_steps), job.elapsed_ns,
                                 job.compute_gpu_ns, job.idle_gpu_ns}) {
            hash_field(hash, value);
        }
    }
    for (const auto& c : report.collectives) {
        for (const auto value : {c.id.value(), static_cast<std::uint64_t>(c.outcome),
                                 c.started.count(), c.finished.count(), c.planned_bytes,
                                 c.issued_fabric_bytes, c.issued_local_bytes, c.delivered_bytes}) {
            hash_field(hash, value);
        }
    }
    for (const auto& r : report.decisions) {
        hash_field(hash, r.request.flow);
        hash_field(hash, r.timestamp.count());
        for (const auto hop : r.path) {
            hash_field(hash, hop.link.value());
            hash_field(hash, static_cast<std::uint64_t>(hop.direction));
        }
    }
    return hash;
}
void train(const Options& options) {
    workload::TrainingScenario scenario;
    const auto nic_count =
        static_cast<std::size_t>(options.workers) * (options.shared_nics ? 1 : options.jobs);
    scenario.gpus = std::max<std::size_t>(64, ((nic_count * 8 + 63) / 64) * 64);
    scenario.buffer = transport::ByteCount{options.buffer};
    for (std::uint32_t job = 0; job < options.jobs; ++job) {
        workload::JobSpec spec;
        spec.name = "synthetic-benchmark";
        spec.steps = options.steps;
        spec.gradient_bytes = transport::ByteCount{options.gradient};
        spec.bucket_bytes = transport::ByteCount{options.bucket};
        spec.chunk_bytes = transport::ByteCount{options.chunk};
        spec.overlap = options.overlap;
        for (std::uint32_t worker = 0; worker < options.workers; ++worker) {
            spec.workers.emplace_back(options.shared_nics ? worker * 8 + job
                                                          : (job * options.workers + worker) * 8);
            spec.compute.emplace_back(options.compute);
        }
        scenario.jobs.push_back(std::move(spec));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto report = workload::run_training(scenario);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::uint64_t successes{0};
    std::uint64_t compute{0};
    std::uint64_t idle{0};
    std::uint64_t issued{0};
    std::uint64_t delivered{0};
    for (const auto& job : report.jobs) {
        successes += static_cast<std::uint64_t>(job.state == workload::JobState::Succeeded);
        compute = workload::checked_sum(compute, job.compute_gpu_ns);
        idle = workload::checked_sum(idle, job.idle_gpu_ns);
    }
    for (const auto& c : report.collectives) {
        issued = workload::checked_sum(
            issued, workload::checked_sum(c.issued_fabric_bytes, c.issued_local_bytes));
        delivered = workload::checked_sum(delivered, c.delivered_bytes);
    }
    std::cout << "elapsed_ns=" << elapsed << "\ngpus=" << scenario.gpus
              << "\nsucceeded_jobs=" << successes
              << "\nfailed_jobs=" << report.jobs.size() - successes
              << "\nevents=" << report.simulation.dispatched_events
              << "\nfinal_time_ns=" << report.simulation.final_time.count()
              << "\ncompute_gpu_ns=" << compute << "\nidle_gpu_ns=" << idle
              << "\nissued_bytes=" << issued << "\ndelivered_bytes=" << delivered
              << "\nmaximum_waiting_bytes=" << report.maximum_waiting_bytes
              << "\nroute_decisions=" << report.decisions.size()
              << "\ncollectives=" << report.collectives.size()
              << "\ndomain_digest=" << digest(report) << '\n';
}
void planning(const Options& options) {
    if (options.workers < 2) {
        throw std::invalid_argument{"planning needs at least two participants"};
    }
    std::uint64_t checksum{0};
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t iteration = 0; iteration < 1000; ++iteration) {
        const auto plan = collective::plan_round(
            options.workers, transport::ByteCount{options.gradient},
            collective::Phase::ReduceScatter, iteration % (options.workers - 1));
        for (const auto& transfer : plan) {
            checksum += transfer.bytes.value() + transfer.shard;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::cout << "planning_total_ns=" << elapsed << "\niterations=1000\nround_plan_payload_bytes="
              << options.workers * sizeof(collective::RingTransfer) << "\nplanned_collective_bytes="
              << collective::planned_volume(options.workers, transport::ByteCount{options.gradient})
              << "\nchecksum=" << checksum << '\n';
}
int run(const Options& options) {
    std::cout << "benchmark=training\nsynthetic=true\nmode=" << options.mode
              << "\nworkers=" << options.workers << "\njobs=" << options.jobs
              << "\nsteps=" << options.steps << "\ngradient_bytes=" << options.gradient
              << "\nbucket_bytes=" << options.bucket << "\nchunk_bytes=" << options.chunk
              << "\ncompute_ns=" << options.compute << "\nbuffer_bytes=" << options.buffer
              << "\nshared_nics=" << options.shared_nics << "\noverlap=" << options.overlap
              << "\nseed=42\nrouting_policy=ecmp\ntrace_mode=disabled\n";
    if (options.mode == "planning") {
        planning(options);
    } else {
        train(options);
    }
    std::cout << "peak_rss_kib=" << peak_rss() << "\nevent_size_bytes=" << sizeof(sim::Event)
              << '\n';
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "training benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
