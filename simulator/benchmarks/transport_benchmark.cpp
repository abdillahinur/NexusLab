// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace sim = nexuslab::sim;
namespace topology = nexuslab::topology;
namespace transport = nexuslab::transport;

struct Options final {
    std::string pattern{"incast"};
    std::uint64_t flows{100};
    std::uint64_t bytes{65'536};
    std::uint64_t chunk_bytes{4'096};
    std::uint64_t buffer_bytes{262'144};
    std::uint64_t gpus{512};
};

[[nodiscard]] std::uint64_t number(std::string_view value) {
    std::uint64_t result{0};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0) {
        throw std::invalid_argument{"benchmark quantities must be positive integers"};
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
        if (key == "--pattern") {
            options.pattern = argv[index + 1];
            continue;
        }
        const auto value = number(argv[index + 1]);
        if (key == "--flows") {
            options.flows = value;
        } else if (key == "--bytes") {
            options.bytes = value;
        } else if (key == "--chunk-bytes") {
            options.chunk_bytes = value;
        } else if (key == "--buffer-bytes") {
            options.buffer_bytes = value;
        } else if (key == "--gpus") {
            options.gpus = value;
        } else {
            throw std::invalid_argument{"unknown benchmark option"};
        }
    }
    if (options.pattern != "pipeline" && options.pattern != "incast" &&
        options.pattern != "all-to-all") {
        throw std::invalid_argument{"pattern must be pipeline, incast or all-to-all"};
    }
    const auto chunks = options.bytes / options.chunk_bytes +
                        static_cast<std::uint64_t>(options.bytes % options.chunk_bytes != 0);
    if (options.flows > 1'000'000 / chunks || options.gpus > 8'192 || options.gpus < 64 ||
        options.bytes > std::numeric_limits<std::uint64_t>::max() / options.flows) {
        throw std::length_error{"benchmark exceeds bounded scale"};
    }
    return options;
}

[[nodiscard]] topology::DirectedLinkId arc_between(topology::NodeId source,
                                                   const topology::TopologyGraph& graph,
                                                   topology::NodeId destination) {
    for (const auto& arc : graph.outgoing(source)) {
        if (graph.find(arc.destination)->owner == destination) {
            return arc.id;
        }
    }
    throw std::logic_error{"benchmark fixed path has no arc"};
}

// The traffic harness supplies fixed routes through spine zero; it is not a routing policy.
[[nodiscard]] std::vector<topology::DirectedLinkId>
fixed_route(topology::NicId source, const topology::TopologyGraph& graph,
            topology::NicId destination) {
    const auto source_node = topology::NodeId{source};
    const auto destination_node = topology::NodeId{destination};
    if (graph.switches().empty()) {
        return {arc_between(source_node, graph, destination_node)};
    }
    const auto source_leaf = graph.find(graph.find(source)->rack)->leaf_switches.front();
    const auto destination_leaf = graph.find(graph.find(destination)->rack)->leaf_switches.front();
    std::vector<topology::DirectedLinkId> route{
        arc_between(source_node, graph, topology::NodeId{source_leaf})};
    if (source_leaf != destination_leaf) {
        const auto spine = topology::SwitchId{graph.racks().size()};
        route.push_back(arc_between(topology::NodeId{source_leaf}, graph, topology::NodeId{spine}));
        route.push_back(
            arc_between(topology::NodeId{spine}, graph, topology::NodeId{destination_leaf}));
    }
    route.push_back(arc_between(topology::NodeId{destination_leaf}, graph, destination_node));
    return route;
}

class Dispatcher final {
  public:
    Dispatcher(transport::TransportRuntime& runtime, const topology::TopologyGraph& graph,
               Options options)
        : runtime_{runtime}, graph_{graph}, options_{std::move(options)} {}
    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        const std::uint64_t nics = graph_.nics().size();
        for (std::uint64_t flow = 0; flow < options_.flows; ++flow) {
            const std::uint64_t source =
                options_.pattern == "all-to-all" ? flow % nics : 1 + flow % (nics - 1);
            const std::uint64_t destination = options_.pattern == "all-to-all"
                                                  ? (source + 1 + (flow / nics) % (nics - 1)) % nics
                                                  : 0;
            const auto source_id = topology::NicId{source};
            const auto destination_id = topology::NicId{destination};
            static_cast<void>(runtime_.submit_transfer(
                {topology::NodeId{source_id}, topology::NodeId{destination_id},
                 transport::ByteCount{options_.bytes}, transport::ByteCount{options_.chunk_bytes},
                 fixed_route(source_id, graph_, destination_id)},
                context));
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

  private:
    transport::TransportRuntime& runtime_;
    const topology::TopologyGraph& graph_;
    Options options_;
};

[[nodiscard]] std::uint64_t peak_rss() {
    std::ifstream input{"/proc/self/status"};
    std::string key;
    while (input >> key) {
        if (key == "VmHWM:") {
            std::uint64_t value{0};
            std::string unit;
            if (!(input >> value >> unit) || unit != "kB") {
                break;
            }
            return value;
        }
        input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    throw std::runtime_error{"cannot read Linux peak RSS"};
}

void report(const Options& options, const topology::TopologyGraph& graph,
            transport::TransportRuntime& runtime, const sim::SimulationResult& result,
            std::int64_t elapsed) {
    transport::TrafficCount delivered{};
    transport::TrafficCount dropped{};
    std::uint64_t succeeded{0};
    const auto outcomes = runtime.take_completed_transfers();
    if (outcomes.size() != options.flows) {
        throw std::logic_error{"missing transfer outcomes"};
    }
    for (const auto& completion : outcomes) {
        delivered = transport::add_traffic(delivered, completion.delivered);
        dropped =
            transport::add_traffic(dropped, transport::add_traffic(completion.dropped_buffer_full,
                                                                   completion.dropped_link_down));
        succeeded +=
            static_cast<std::uint64_t>(completion.outcome == transport::TransferOutcome::Succeeded);
    }
    if (options.bytes > std::numeric_limits<std::uint64_t>::max() / options.flows ||
        transport::add_traffic(delivered, dropped).bytes != options.bytes * options.flows) {
        throw std::logic_error{"benchmark byte accounting mismatch"};
    }
    std::uint64_t max_waiting{0};
    std::uint64_t busy_ns{0};
    for (const auto& link : graph.links()) {
        if (link.kind != topology::LinkKind::Fabric) {
            continue;
        }
        for (const auto& arc : topology::directed_links(link)) {
            const auto* service = runtime.find_service(arc.id);
            max_waiting =
                std::max(max_waiting, service->queue().snapshot().maximum_waiting_bytes.value());
            busy_ns =
                transport::add_traffic(
                    {busy_ns, 0}, {service->statistics(result.final_time).busy_time.count(), 0})
                    .bytes;
        }
    }
    std::cout << "benchmark=transport\nsynthetic=true\nseed=42\npattern=" << options.pattern
              << "\ngpus=" << graph.gpus().size() << "\nflows=" << options.flows
              << "\nbytes_per_flow=" << options.bytes << "\nchunk_bytes=" << options.chunk_bytes
              << "\nbuffer_bytes=" << options.buffer_bytes
              << "\nbandwidth_bps=100000000000\npropagation_ns=500"
              << "\ntrace_mode=disabled\nelapsed_ns=" << elapsed
              << "\nevents=" << result.dispatched_events
              << "\nfinal_time_ns=" << result.final_time.count() << "\nsucceeded=" << succeeded
              << "\nfailed=" << options.flows - succeeded << "\ndelivered_bytes=" << delivered.bytes
              << "\ndropped_bytes=" << dropped.bytes << "\nmaximum_waiting_bytes=" << max_waiting
              << "\naggregate_busy_ns=" << busy_ns << "\npeak_rss_kib=" << peak_rss()
              << "\nevent_size_bytes=" << sizeof(sim::Event) << '\n';
}

int run(const Options& options) {
    auto graph = options.pattern == "pipeline"
                     ? topology::generate_two_gpu_direct()
                     : topology::generate_clos({static_cast<std::size_t>(options.gpus), 8, 8, 8});
    std::vector<transport::DirectedLinkConfiguration> configurations;
    for (const auto& link : graph->links()) {
        if (link.kind != topology::LinkKind::Fabric) {
            continue;
        }
        for (const auto& arc : topology::directed_links(link)) {
            configurations.push_back({arc.id, transport::BitsPerSecond{100'000'000'000ULL},
                                      sim::SimDurationNs{500},
                                      transport::ByteCount{options.buffer_bytes}, std::nullopt});
        }
    }
    transport::TransportRuntime runtime{*graph, configurations};
    sim::Simulation simulation{42, sim::TraceMode::Disabled};
    Dispatcher dispatcher{runtime, *graph, options};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    const auto started = std::chrono::steady_clock::now();
    const auto result = simulation.run(dispatcher);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    if (result.status != sim::SimulationStatus::Completed || result.pending_events != 0) {
        throw std::runtime_error{result.error.value_or("transport benchmark failed")};
    }
    report(options, *graph, runtime, result, elapsed);
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "transport benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
