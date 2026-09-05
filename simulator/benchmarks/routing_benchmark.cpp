// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/routing/router.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/topology/clos.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
namespace routing = nexuslab::routing;
namespace topology = nexuslab::topology;
namespace transport = nexuslab::transport;
namespace sim = nexuslab::sim;
using Clock = std::chrono::steady_clock;
struct Options final {
    std::string mode{"traffic"};
    std::string policy{"ecmp"};
    std::string pattern{"all-to-all"};
    std::size_t gpus{512};
    std::uint64_t flows{1000};
    std::uint64_t spacing_ns{100};
    std::uint64_t seed{42};
};
[[nodiscard]] std::uint64_t number(std::string_view value) {
    std::uint64_t result{0};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument{"expected unsigned integer"};
    }
    return result;
}
[[nodiscard]] Options parse(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            throw std::invalid_argument{"expected option value"};
        }
        const std::string_view key{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (key == "--mode") {
            options.mode = value;
        } else if (key == "--policy") {
            options.policy = value;
        } else if (key == "--pattern") {
            options.pattern = value;
        } else if (key == "--gpus") {
            options.gpus = static_cast<std::size_t>(number(value));
        } else if (key == "--flows") {
            options.flows = number(value);
        } else if (key == "--spacing-ns") {
            options.spacing_ns = number(value);
        } else if (key == "--seed") {
            options.seed = number(value);
        } else {
            throw std::invalid_argument{"unknown routing benchmark option"};
        }
    }
    if ((options.mode != "traffic" && options.mode != "lookup") ||
        (options.pattern != "all-to-all" && options.pattern != "incast") || options.gpus < 64 ||
        options.gpus > 8192 || options.flows == 0 || options.flows > 10000 ||
        options.spacing_ns > 1'000'000) {
        throw std::invalid_argument{"routing benchmark exceeds declared input bounds"};
    }
    return options;
}
[[nodiscard]] std::int64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
[[nodiscard]] std::uint64_t peak_rss() {
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
    throw std::runtime_error{"Linux peak RSS unavailable"};
}
[[nodiscard]] std::vector<transport::DirectedLinkConfiguration>
configure(const topology::TopologyGraph& graph) {
    std::vector<transport::DirectedLinkConfiguration> result;
    for (const auto& link : graph.links()) {
        if (link.kind == topology::LinkKind::Fabric) {
            for (const auto& arc : topology::directed_links(link)) {
                result.push_back({arc.id, transport::BitsPerSecond{100'000'000'000ULL},
                                  sim::SimDurationNs{500}, transport::ByteCount{262'144},
                                  std::nullopt});
            }
        }
    }
    return result;
}
void cache_report(routing::CacheStatistics stats) {
    std::cout << "cache_hits=" << stats.hits << "\ncache_misses=" << stats.misses
              << "\ncache_invalidations=" << stats.invalidations << "\ncache_pairs=" << stats.pairs
              << "\ncache_paths=" << stats.paths << "\ncache_route_entries=" << stats.route_entries
              << "\ncache_route_payload_bytes="
              << stats.route_entries * sizeof(topology::DirectedLinkId) << '\n';
}
void lookup(const Options& options, const topology::TopologyGraph& graph,
            const transport::TransportRuntime& runtime) {
    routing::PathService paths{graph};
    const routing::Endpoints pair{topology::NodeId{topology::NicId{0}},
                                  topology::NodeId{graph.nics().back().id}};
    const auto cold_start = Clock::now();
    const auto& candidates = paths.lookup(pair);
    const auto cold_ns = elapsed(cold_start);
    std::uint64_t checksum{0};
    const auto warm_start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < options.flows; ++iteration) {
        checksum += paths.lookup(pair).size();
    }
    const auto warm_ns = elapsed(warm_start);
    const auto policy = routing::PolicyRegistry{}.create(options.policy);
    const auto selection_start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < options.flows; ++iteration) {
        const routing::RouteRequest req{iteration, pair, transport::ByteCount{65'536},
                                        transport::ByteCount{4096}};
        checksum +=
            policy->choose({req, options.seed, candidates, routing::FabricView{runtime}}).candidate;
    }
    const auto selection_ns = elapsed(selection_start);
    // Exercise lazy cache occupancy without all-pairs precomputation.
    for (std::size_t destination = 1; destination < graph.nics().size(); ++destination) {
        static_cast<void>(
            paths.lookup({pair.source, topology::NodeId{topology::NicId{destination}}}));
    }
    std::cout << "cold_lookup_ns=" << cold_ns << "\nwarm_lookup_total_ns=" << warm_ns
              << "\nselection_total_ns=" << selection_ns << "\niterations=" << options.flows
              << "\nchecksum=" << checksum << "\nfabric_view_bytes=" << sizeof(routing::FabricView)
              << '\n';
    cache_report(paths.statistics());
}
class Dispatcher final {
  public:
    Dispatcher(transport::TransportRuntime& runtime, routing::Router& router, std::size_t nics,
               const Options& options)
        : runtime_{runtime}, router_{router}, nics_{nics}, options_{options} {}
    void operator()(const sim::NoOpEvent& event, sim::SimulationContext& context) {
        const auto flow = event.token;
        const auto source = options_.pattern == "incast" ? 1 + flow % (nics_ - 1) : flow % nics_;
        const auto destination = options_.pattern == "incast"
                                     ? 0
                                     : (source + nics_ / 2 + (flow / nics_) % (nics_ / 2)) % nics_;
        const routing::RouteRequest req{flow,
                                        {topology::NodeId{topology::NicId{source}},
                                         topology::NodeId{topology::NicId{destination}}},
                                        transport::ByteCount{65'536},
                                        transport::ByteCount{4096}};
        if (!router_.submit(req, context).has_value()) {
            throw std::logic_error{"connected benchmark returned no route"};
        }
        if (flow + 1 < options_.flows) {
            static_cast<void>(
                context.schedule({sim::SimTimeNs{(flow + 1) * options_.spacing_ns},
                                  sim::EventPriority::Normal, sim::NoOpEvent{flow + 1}}));
        }
    }
    void operator()(const transport::ChunkArrivalEvent& e, sim::SimulationContext& c) {
        runtime_.handle_arrival(e, c);
    }
    void operator()(const transport::TransmissionCompleteEvent& e, sim::SimulationContext& c) {
        runtime_.handle_completion(e, c);
    }
    void operator()(const transport::LinkStateChangeEvent& e, sim::SimulationContext& c) {
        runtime_.handle_link_state_change(e, c);
    }
    void operator()(const transport::PortStateChangeEvent& e, sim::SimulationContext& c) {
        runtime_.handle_port_state_change(e, c);
    }
    void operator()(const transport::SwitchStateChangeEvent& e, sim::SimulationContext& c) {
        runtime_.handle_switch_state_change(e, c);
    }
    void operator()(const nexuslab::workload::WorkloadEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected workload event in this dispatcher"};
    }
    void operator()(const nexuslab::collective::LocalCompletionEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected collective event in this dispatcher"};
    }

  private:
    transport::TransportRuntime& runtime_;
    routing::Router& router_;
    std::size_t nics_;
    const Options& options_;
};
void hash_field(std::uint64_t& hash, std::uint64_t field) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= (field >> shift) & 255U;
        hash *= 1099511628211ULL;
    }
}
[[nodiscard]] std::uint64_t decision_digest(std::span<const routing::RouteDecision> decisions) {
    std::uint64_t hash{14695981039346656037ULL};
    for (const auto& decision : decisions) {
        hash_field(hash, decision.request.flow);
        hash_field(hash, decision.timestamp.count());
        hash_field(hash, decision.operational_revision);
        hash_field(hash, decision.candidates);
        hash_field(hash, decision.score);
        hash_field(hash, decision.path.size());
        for (const auto link : decision.path) {
            hash_field(hash, link.link.value());
            hash_field(hash, static_cast<std::uint64_t>(link.direction));
        }
    }
    return hash;
}
void traffic_report(const Options& options, const topology::TopologyGraph& graph,
                    transport::TransportRuntime& runtime, const routing::Router& router) {
    transport::TrafficCount delivered{};
    transport::TrafficCount dropped{};
    std::vector<std::uint64_t> completion_ns;
    std::uint64_t outcome_digest{14695981039346656037ULL};
    std::uint64_t outcome_count{0};
    for (const auto& completion : runtime.take_completed_transfers()) {
        ++outcome_count;
        for (const auto field :
             {completion.transfer.value(), static_cast<std::uint64_t>(completion.outcome),
              completion.timestamp.count(), completion.delivered.bytes, completion.delivered.chunks,
              completion.dropped_buffer_full.bytes, completion.dropped_buffer_full.chunks,
              completion.dropped_link_down.bytes, completion.dropped_link_down.chunks}) {
            hash_field(outcome_digest, field);
        }
        delivered = transport::add_traffic(delivered, completion.delivered);
        dropped =
            transport::add_traffic(dropped, transport::add_traffic(completion.dropped_buffer_full,
                                                                   completion.dropped_link_down));
        if (completion.outcome == transport::TransferOutcome::Succeeded) {
            completion_ns.push_back(completion.timestamp.count() -
                                    completion.transfer.value() * options.spacing_ns);
        }
    }
    if (outcome_count != options.flows ||
        delivered.bytes + dropped.bytes != options.flows * 65'536 ||
        router.decisions().size() != options.flows) {
        throw std::logic_error{"routing comparison conservation mismatch"};
    }
    std::ranges::sort(completion_ns);
    std::uint64_t maximum_waiting{0};
    for (const auto& link : graph.links()) {
        if (link.kind == topology::LinkKind::Fabric) {
            for (const auto& arc : topology::directed_links(link)) {
                maximum_waiting = std::max(
                    maximum_waiting,
                    runtime.find_service(arc.id)->queue().snapshot().maximum_waiting_bytes.value());
            }
        }
    }
    std::cout << "succeeded=" << completion_ns.size()
              << "\nfailed=" << options.flows - completion_ns.size()
              << "\ndelivered_bytes=" << delivered.bytes << "\ndropped_bytes=" << dropped.bytes
              << "\nmaximum_waiting_bytes=" << maximum_waiting << "\nsuccess_p50_ns="
              << (completion_ns.empty() ? 0 : completion_ns[(completion_ns.size() - 1) / 2])
              << "\nsuccess_p95_ns="
              << (completion_ns.empty() ? 0
                                        : completion_ns[(completion_ns.size() * 95 + 99) / 100 - 1])
              << "\noutcome_digest=" << outcome_digest
              << "\ndecision_digest=" << decision_digest(router.decisions()) << '\n';
    cache_report(router.cache_statistics());
}
void traffic(const Options& options, const topology::TopologyGraph& graph,
             transport::TransportRuntime& runtime) {
    routing::Router router{
        graph, runtime, routing::PolicyRegistry{}, {options.policy, options.seed}};
    Dispatcher dispatcher{runtime, router, graph.nics().size(), options};
    sim::Simulation simulation{options.seed, sim::TraceMode::Disabled};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    const auto start = Clock::now();
    const auto result = simulation.run(dispatcher);
    const auto runtime_ns = elapsed(start);
    if (result.status != sim::SimulationStatus::Completed) {
        throw std::runtime_error{result.error.value_or("routing run incomplete")};
    }
    std::cout << "elapsed_ns=" << runtime_ns << "\nevents=" << result.dispatched_events
              << "\nfinal_time_ns=" << result.final_time.count() << '\n';
    traffic_report(options, graph, runtime, router);
}
int run(const Options& options) {
    auto graph = topology::generate_clos({options.gpus, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configure(*graph)};
    std::cout << "benchmark=routing\nsynthetic=true\nmode=" << options.mode
              << "\npolicy=" << options.policy << "\npolicy_version=1\ngpus=" << options.gpus
              << "\nflows=" << options.flows << "\npattern=" << options.pattern
              << "\nspacing_ns=" << options.spacing_ns << "\nseed=" << options.seed
              << "\nbytes_per_flow=65536\nchunk_bytes=4096\nbuffer_bytes=262144\nbandwidth_bps="
                 "100000000000\npropagation_ns=500\ntrace_mode=disabled\n";
    if (options.mode == "lookup") {
        lookup(options, *graph, runtime);
    } else {
        traffic(options, *graph, runtime);
    }
    std::cout << "peak_rss_kib=" << peak_rss() << '\n';
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "routing benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
