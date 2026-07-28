// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using nexuslab::sim::EventPayload;
using nexuslab::sim::EventPriority;
using nexuslab::sim::EventSpec;
using nexuslab::sim::NoOpEvent;
using nexuslab::sim::SimTimeNs;
using nexuslab::sim::Simulation;
using nexuslab::sim::SimulationContext;
using nexuslab::sim::SimulationResult;
using nexuslab::sim::SimulationStatus;
using nexuslab::sim::TraceMode;

constexpr std::uint64_t default_event_count = 1'000'000;

class BenchmarkDispatcher final {
  public:
    explicit BenchmarkDispatcher(std::uint64_t& checksum) noexcept : checksum_{&checksum} {}

    void operator()(const NoOpEvent& event, SimulationContext& context) const noexcept {
        static_cast<void>(context);
        *checksum_ ^= event.token + 0x9e3779b97f4a7c15ULL + (*checksum_ << 6U) + (*checksum_ >> 2U);
    }

    void operator()(const nexuslab::transport::ChunkArrivalEvent& event,
                    SimulationContext& context) const {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"unexpected chunk-arrival event in simulation benchmark"};
    }

    void operator()(const nexuslab::transport::TransmissionCompleteEvent& event,
                    SimulationContext& context) const {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"unexpected transmission-completion event in simulation benchmark"};
    }

  private:
    std::uint64_t* checksum_;
};

struct BenchmarkResult final {
    std::uint64_t event_count;
    std::uint64_t insertion_elapsed_ns;
    std::uint64_t dispatch_elapsed_ns;
    std::uint64_t rss_before_kib;
    std::uint64_t rss_after_insertion_kib;
    std::uint64_t rss_after_dispatch_kib;
    std::uint64_t peak_rss_kib;
    std::uint64_t checksum;
    SimulationResult simulation;
};

[[nodiscard]] bool parse_event_count(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
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

[[nodiscard]] BenchmarkResult benchmark(std::uint64_t event_count) {
    Simulation simulation{42, TraceMode::Disabled};
    const std::uint64_t rss_before_kib = current_rss_kib();

    const auto insertion_started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < event_count; ++index) {
        static_cast<void>(simulation.schedule(
            EventSpec{SimTimeNs{index}, EventPriority::Normal, EventPayload{NoOpEvent{index}}}));
    }
    const auto insertion_finished = std::chrono::steady_clock::now();
    const std::uint64_t rss_after_insertion_kib = current_rss_kib();

    std::uint64_t checksum = 0;
    BenchmarkDispatcher dispatcher{checksum};
    const auto dispatch_started = std::chrono::steady_clock::now();
    SimulationResult result = simulation.run(dispatcher);
    const auto dispatch_finished = std::chrono::steady_clock::now();
    const std::uint64_t rss_after_dispatch_kib = current_rss_kib();

    if (result.status != SimulationStatus::Completed || result.dispatched_events != event_count ||
        result.pending_events != 0 || result.final_time != SimTimeNs{event_count - 1}) {
        throw std::runtime_error{"simulation benchmark produced an invalid result"};
    }

    return BenchmarkResult{event_count,
                           elapsed_nanoseconds(insertion_started, insertion_finished),
                           elapsed_nanoseconds(dispatch_started, dispatch_finished),
                           rss_before_kib,
                           rss_after_insertion_kib,
                           rss_after_dispatch_kib,
                           peak_rss_kib(),
                           checksum,
                           result};
}

[[nodiscard]] double events_per_second(std::uint64_t event_count, std::uint64_t elapsed_ns) {
    constexpr double nanoseconds_per_second = 1'000'000'000.0;
    return (static_cast<double>(event_count) * nanoseconds_per_second) /
           static_cast<double>(elapsed_ns);
}

void print_result(const BenchmarkResult& result) {
    const std::uint64_t rss_insertion_delta_kib =
        result.rss_after_insertion_kib >= result.rss_before_kib
            ? result.rss_after_insertion_kib - result.rss_before_kib
            : 0;

    std::cout << "benchmark=simulation_core\n"
              << "events=" << result.event_count << '\n'
              << "trace_mode=disabled\n"
              << "memory_source=/proc/self/status\n"
              << "event_size_bytes=" << sizeof(nexuslab::sim::Event) << '\n'
              << "event_payload_size_bytes=" << sizeof(EventPayload) << '\n'
              << "insertion_elapsed_ns=" << result.insertion_elapsed_ns << '\n'
              << std::fixed << std::setprecision(2) << "insertion_events_per_second="
              << events_per_second(result.event_count, result.insertion_elapsed_ns) << '\n'
              << "dispatch_elapsed_ns=" << result.dispatch_elapsed_ns << '\n'
              << "dispatch_events_per_second="
              << events_per_second(result.event_count, result.dispatch_elapsed_ns) << '\n'
              << "rss_before_kib=" << result.rss_before_kib << '\n'
              << "rss_after_insertion_kib=" << result.rss_after_insertion_kib << '\n'
              << "rss_after_dispatch_kib=" << result.rss_after_dispatch_kib << '\n'
              << "rss_insertion_delta_kib=" << rss_insertion_delta_kib << '\n'
              << "peak_rss_kib=" << result.peak_rss_kib << '\n'
              << "dispatched_events=" << result.simulation.dispatched_events << '\n'
              << "final_time_ns=" << result.simulation.final_time.count() << '\n'
              << "checksum=" << result.checksum << '\n';
}

void print_usage(std::ostream& output) {
    output << "Usage: nexuslab_benchmarks [--events COUNT]\n";
}

int run(int argc, char** argv) {
    std::uint64_t event_count = default_event_count;

    if (argc == 3 && std::string_view{argv[1]} == "--events") {
        if (!parse_event_count(argv[2], event_count)) {
            std::cerr << "events must be a positive unsigned integer\n";
            return 2;
        }
    } else if (argc != 1) {
        print_usage(std::cerr);
        return 2;
    }

    print_result(benchmark(event_count));
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "benchmark failed: %s\n", error.what());
    } catch (...) {
        std::fputs("benchmark failed: unknown error\n", stderr);
    }
    return 1;
}
