// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/serialization.hpp"
#include "nexuslab/workload/digest.hpp"
#include "nexuslab/workload/scenario.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace telemetry = nexuslab::telemetry;
namespace workload = nexuslab::workload;

constexpr std::string_view benchmark_scenario_id = "cluster-8-telemetry-overhead-v1";

[[nodiscard]] telemetry::TelemetryMode parse_options(int argc, char** argv) {
    if (argc != 3 || std::string_view{argv[1]} != "--mode") {
        throw std::invalid_argument{
            "usage: nexuslab_telemetry_benchmarks --mode <off|summary|sampled|full>"};
    }
    return telemetry::parse_mode(argv[2]);
}

[[nodiscard]] std::uint64_t peak_rss_kib() {
    std::ifstream input{"/proc/self/status"};
    std::string key;
    while (input >> key) {
        if (key == "VmHWM:") {
            std::uint64_t value = 0;
            std::string unit;
            if (input >> value >> unit && unit == "kB") {
                return value;
            }
            break;
        }
        input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    throw std::runtime_error{"Linux peak RSS is unavailable"};
}

[[nodiscard]] workload::TrainingScenario scenario(telemetry::TelemetryMode mode) {
    auto result = workload::parse_scenario("version: 1\n"
                                           "gpus: 512\n"
                                           "seed: 42\n"
                                           "routing_policy: ecmp\n"
                                           "scheduling_policy: first-fit\n"
                                           "jobs:\n"
                                           "  - name: telemetry-overhead-baseline\n"
                                           "    requested_workers: 16\n"
                                           "    compute_ns: 10000\n"
                                           "    steps: 2\n"
                                           "    gradient_bytes: 65536\n"
                                           "    bucket_bytes: 16384\n"
                                           "    chunk_bytes: 4096\n"
                                           "    overlap: true\n");
    result.telemetry.mode = mode;
    result.telemetry.sample_interval_ns = 100'000;
    return result;
}

[[nodiscard]] telemetry::TelemetryRunMetadata
metadata(const workload::TrainingScenario& scenario_value, const workload::TrainingReport& report) {
    if (!scenario_value.scheduling.has_value()) {
        throw std::logic_error{"telemetry benchmark requires scheduling"};
    }
    telemetry::TelemetryRunMetadata result;
    result.seed = scenario_value.seed;
    result.scenario_digest = telemetry::fnv1a64(benchmark_scenario_id);
    result.routing_policy = scenario_value.routing_policy;
    result.scheduling_policy = scenario_value.scheduling.value().policy;
    result.final_time = report.simulation.final_time;
    return result;
}

[[nodiscard]] std::uint64_t events_per_second(std::uint64_t events,
                                              std::uint64_t elapsed_ns) noexcept {
    if (elapsed_ns == 0) {
        return 0;
    }
    constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;
    if (events > std::numeric_limits<std::uint64_t>::max() / nanoseconds_per_second) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (events * nanoseconds_per_second) / elapsed_ns;
}

int run(telemetry::TelemetryMode mode) {
    const workload::TrainingScenario scenario_value = scenario(mode);
    if (!scenario_value.scheduling.has_value()) {
        throw std::logic_error{"telemetry benchmark requires scheduling"};
    }
    const auto start = std::chrono::steady_clock::now();
    const workload::TrainingReport report = workload::run_training(scenario_value);
    const auto elapsed_signed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
    if (elapsed_signed < 0) {
        throw std::runtime_error{"steady clock elapsed time regressed"};
    }
    const auto elapsed_ns = static_cast<std::uint64_t>(elapsed_signed);
    const telemetry::TelemetryRunMetadata run_metadata = metadata(scenario_value, report);
    const std::string summary = telemetry::serialize_summary_json(report.telemetry, run_metadata);
    const std::string records = telemetry::serialize_records_jsonl(report.telemetry, run_metadata);
    const std::uint64_t serialized_bytes =
        static_cast<std::uint64_t>(summary.size()) + static_cast<std::uint64_t>(records.size());
    const telemetry::TelemetryConfiguration& configuration = report.telemetry.configuration;

    std::cout << "benchmark=telemetry_overhead\nsynthetic=true\nmode="
              << telemetry::mode_name(configuration.mode)
              << "\ngpus=512\nworkers=16\nsteps=2\nseed=" << scenario_value.seed
              << "\nscenario_digest_fnv1a64=" << run_metadata.scenario_digest
              << "\nrouting_policy=" << scenario_value.routing_policy
              << "\nscheduling_policy=" << scenario_value.scheduling.value().policy
              << "\nsample_interval_ns=" << configuration.sample_interval_ns
              << "\nlimit_metric_series=" << configuration.limits.metric_series
              << "\nlimit_decision_records=" << configuration.limits.decision_records
              << "\nlimit_domain_records=" << configuration.limits.domain_records
              << "\nlimit_samples=" << configuration.limits.samples
              << "\nlimit_histogram_boundaries=" << configuration.limits.histogram_boundaries
              << "\nlimit_correlation_edges=" << configuration.limits.correlation_edges
              << "\nlimit_serialized_bytes=" << configuration.limits.serialized_bytes
              << "\nelapsed_ns=" << elapsed_ns << "\nevents=" << report.simulation.dispatched_events
              << "\nevents_per_second="
              << events_per_second(report.simulation.dispatched_events, elapsed_ns)
              << "\npeak_rss_kib=" << peak_rss_kib()
              << "\nretained_records=" << report.telemetry.records.size()
              << "\nretained_samples=" << report.telemetry.samples.size()
              << "\nsummary_json_bytes=" << summary.size()
              << "\nrecords_jsonl_bytes=" << records.size()
              << "\nserialized_bytes=" << serialized_bytes
              << "\ndomain_digest_version=" << workload::domain_outcome_digest_version
              << "\ndomain_digest=" << workload::domain_outcome_digest(report) << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "telemetry benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
