// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/export.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/topology/validation.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using nexuslab::topology::ClosConfig;
using nexuslab::topology::ClosDimensions;
using nexuslab::topology::GpuId;
using nexuslab::topology::NodeId;
using nexuslab::topology::ShortestPathSummary;
using nexuslab::topology::TopologyGraph;

constexpr std::size_t default_gpu_count = 512U;

struct TopologyBenchmarkResult final {
    ClosDimensions dimensions;
    std::uint64_t construction_elapsed_ns;
    std::uint64_t validation_elapsed_ns;
    std::uint64_t shortest_path_query_elapsed_ns;
    std::uint64_t serialization_elapsed_ns;
    std::uint64_t rss_before_kib;
    std::uint64_t rss_after_construction_kib;
    std::uint64_t rss_after_serialization_kib;
    std::uint64_t peak_rss_kib;
    std::size_t serialization_bytes;
    ShortestPathSummary path;
};

[[nodiscard]] bool parse_gpu_count(std::string_view text, std::size_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0U;
}

[[nodiscard]] std::uint64_t elapsed_nanoseconds(std::chrono::steady_clock::time_point started,
                                                std::chrono::steady_clock::time_point finished) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
    return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 1U;
}

[[nodiscard]] std::uint64_t proc_status_kib(std::string_view field) {
    std::ifstream status{"/proc/self/status"};
    std::string label;
    while (status >> label) {
        if (label == field) {
            std::uint64_t value = 0;
            std::string unit;
            if (!(status >> value >> unit) || unit != "kB") {
                throw std::runtime_error{"invalid memory field in /proc/self/status"};
            }
            return value;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    throw std::runtime_error{"missing memory field in /proc/self/status"};
}

[[nodiscard]] std::uint64_t current_rss_kib() { return proc_status_kib("VmRSS:"); }

[[nodiscard]] std::uint64_t peak_rss_kib() { return proc_status_kib("VmHWM:"); }

[[nodiscard]] TopologyBenchmarkResult benchmark(std::size_t gpu_count) {
    const ClosConfig config{gpu_count, 8U, 8U, 8U};
    const ClosDimensions dimensions = nexuslab::topology::clos_dimensions(config);
    const std::uint64_t rss_before_kib = current_rss_kib();

    const auto construction_started = std::chrono::steady_clock::now();
    const std::unique_ptr<TopologyGraph> graph = nexuslab::topology::generate_clos(config);
    const auto construction_finished = std::chrono::steady_clock::now();
    const std::uint64_t rss_after_construction_kib = current_rss_kib();

    const auto validation_started = std::chrono::steady_clock::now();
    const auto validation = nexuslab::topology::validate_clos_topology(*graph, config);
    const auto validation_finished = std::chrono::steady_clock::now();
    if (!validation.valid()) {
        throw std::runtime_error{"generated topology failed Clos validation"};
    }

    const auto path_started = std::chrono::steady_clock::now();
    const std::optional<ShortestPathSummary> path =
        graph->shortest_path_summary(NodeId{GpuId{0U}}, NodeId{GpuId{gpu_count - 1U}});
    const auto path_finished = std::chrono::steady_clock::now();
    if (!path.has_value()) {
        throw std::runtime_error{"generated topology has no representative GPU path"};
    }

    const auto serialization_started = std::chrono::steady_clock::now();
    const std::string serialized = nexuslab::topology::serialize_topology_yaml(*graph);
    const auto serialization_finished = std::chrono::steady_clock::now();
    const std::uint64_t rss_after_serialization_kib = current_rss_kib();

    return TopologyBenchmarkResult{
        dimensions,
        elapsed_nanoseconds(construction_started, construction_finished),
        elapsed_nanoseconds(validation_started, validation_finished),
        elapsed_nanoseconds(path_started, path_finished),
        elapsed_nanoseconds(serialization_started, serialization_finished),
        rss_before_kib,
        rss_after_construction_kib,
        rss_after_serialization_kib,
        peak_rss_kib(),
        serialized.size(),
        *path,
    };
}

void print_result(const TopologyBenchmarkResult& result) {
    const std::uint64_t rss_construction_delta_kib =
        result.rss_after_construction_kib >= result.rss_before_kib
            ? result.rss_after_construction_kib - result.rss_before_kib
            : 0U;
    std::cout << "benchmark=topology\n"
              << "gpus=" << result.dimensions.gpu_count << '\n'
              << "nics=" << result.dimensions.nic_count << '\n'
              << "leaf_switches=" << result.dimensions.leaf_count << '\n'
              << "spine_switches=" << result.dimensions.spine_count << '\n'
              << "ports=" << result.dimensions.port_count << '\n'
              << "links=" << result.dimensions.link_count << '\n'
              << "shortest_path_mode=on_demand_bfs\n"
              << "shortest_path_preprocessing_elapsed_ns=0\n"
              << "construction_elapsed_ns=" << result.construction_elapsed_ns << '\n'
              << "validation_elapsed_ns=" << result.validation_elapsed_ns << '\n'
              << "shortest_path_query_elapsed_ns=" << result.shortest_path_query_elapsed_ns << '\n'
              << "representative_path_hops=" << result.path.hops << '\n'
              << "representative_equal_cost_paths=" << result.path.equal_cost_paths << '\n'
              << "serialization_elapsed_ns=" << result.serialization_elapsed_ns << '\n'
              << "serialization_bytes=" << result.serialization_bytes << '\n'
              << "memory_source=/proc/self/status\n"
              << "rss_before_kib=" << result.rss_before_kib << '\n'
              << "rss_after_construction_kib=" << result.rss_after_construction_kib << '\n'
              << "rss_construction_delta_kib=" << rss_construction_delta_kib << '\n'
              << "rss_after_serialization_kib=" << result.rss_after_serialization_kib << '\n'
              << "peak_rss_kib=" << result.peak_rss_kib << '\n';
}

void print_usage(std::ostream& output) {
    output << "Usage: nexuslab_topology_benchmarks [--gpus COUNT]\n";
}

int run(int argc, char** argv) {
    std::size_t gpu_count = default_gpu_count;
    if (argc == 3 && std::string_view{argv[1]} == "--gpus") {
        if (!parse_gpu_count(argv[2], gpu_count)) {
            std::cerr << "GPU count must be a positive unsigned integer\n";
            return 2;
        }
    } else if (argc != 1) {
        print_usage(std::cerr);
        return 2;
    }

    print_result(benchmark(gpu_count));
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "topology benchmark failed: %s\n", error.what());
    } catch (...) {
        std::fputs("topology benchmark failed: unknown error\n", stderr);
    }
    return 1;
}
