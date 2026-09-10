// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/serialization.hpp"
#include "nexuslab/telemetry/summary.hpp"
#include "nexuslab/workload/digest.hpp"
#include "nexuslab/workload/scenario.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace nexuslab::telemetry {
namespace {

using ::testing::IsEmpty;

[[nodiscard]] workload::TrainingScenario validation_scenario(TelemetryMode mode) {
    auto scenario = workload::parse_scenario("version: 1\n"
                                             "gpus: 64\n"
                                             "seed: 42\n"
                                             "routing_policy: queue-aware\n"
                                             "scheduling_policy: first-fit\n"
                                             "jobs:\n"
                                             "  - name: telemetry-validation\n"
                                             "    requested_workers: 4\n"
                                             "    compute_ns: [10000, 12000, 14000, 16000]\n"
                                             "    steps: 2\n"
                                             "    gradient_bytes: 4096\n"
                                             "    bucket_bytes: 2048\n"
                                             "    chunk_bytes: 1024\n"
                                             "    overlap: true\n");
    scenario.telemetry.mode = mode;
    scenario.telemetry.sample_interval_ns = 1'000;
    return scenario;
}

[[nodiscard]] TelemetryRunMetadata metadata(const workload::TrainingScenario& scenario,
                                            const workload::TrainingReport& report) {
    if (!scenario.scheduling.has_value()) {
        throw std::logic_error{"telemetry validation scenario requires scheduling"};
    }
    TelemetryRunMetadata result;
    result.seed = scenario.seed;
    result.scenario_digest = 0xC8U;
    result.routing_policy = scenario.routing_policy;
    result.scheduling_policy = scenario.scheduling.value().policy;
    result.final_time = report.simulation.final_time;
    return result;
}

[[nodiscard]] MetricSeriesSnapshot required_metric(const TelemetrySnapshot& snapshot,
                                                   MetricId metric,
                                                   const MetricLabels& labels = {}) {
    for (const MetricSeriesSnapshot& series : snapshot.metrics) {
        if (series.metric == metric && series.labels == labels) {
            return series;
        }
    }
    throw std::logic_error{"required telemetry validation metric is absent"};
}

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(TelemetryValidationTest, DomainOutcomesAreIdenticalAcrossAllTelemetryModes) {
    constexpr std::array modes{TelemetryMode::Off, TelemetryMode::Summary, TelemetryMode::Sampled,
                               TelemetryMode::Full};
    std::array<std::uint64_t, modes.size()> digests{};
    std::array<workload::TrainingReport, modes.size()> reports;

    for (std::size_t index = 0; index < modes.size(); ++index) {
        reports[index] = workload::run_training(validation_scenario(modes[index]));
        digests[index] = workload::domain_outcome_digest(reports[index]);
    }

    EXPECT_THAT(digests, ::testing::Each(digests.front()));
    EXPECT_THAT(reports[0].telemetry.metrics, IsEmpty());
    EXPECT_THAT(reports[1].telemetry.records, IsEmpty());
    EXPECT_FALSE(reports[2].telemetry.records.empty());
    EXPECT_FALSE(reports[2].telemetry.samples.empty());
    EXPECT_FALSE(reports[3].telemetry.records.empty());
    EXPECT_FALSE(reports[3].telemetry.samples.empty());
    EXPECT_FALSE(reports[0].simulation.trace_hash.has_value());
    EXPECT_FALSE(reports[1].simulation.trace_hash.has_value());
    EXPECT_FALSE(reports[2].simulation.trace_hash.has_value());
    EXPECT_TRUE(reports[3].simulation.trace_hash.has_value());
}

TEST(TelemetryValidationTest, FullTraceRebuildMatchesTheLiveCanonicalSummary) {
    const workload::TrainingScenario scenario = validation_scenario(TelemetryMode::Full);
    const workload::TrainingReport report = workload::run_training(scenario);
    SummaryBuilder rebuilt{scenario.telemetry.limits.metric_series,
                           scenario.telemetry.limits.histogram_boundaries};
    std::uint64_t expected_record_id = 0;
    for (const TelemetryRecord& record : report.telemetry.records) {
        ASSERT_EQ(record.id.value(), expected_record_id++);
        rebuilt.consume(record.timestamp, record.correlation, record.observation);
    }
    rebuilt.finalize(report.simulation.final_time);

    TelemetrySnapshot rebuilt_snapshot = report.telemetry;
    rebuilt_snapshot.metrics = rebuilt.metric_snapshots();
    rebuilt_snapshot.job_attributions = rebuilt.job_attributions();

    EXPECT_EQ(rebuilt_snapshot.metrics, report.telemetry.metrics);
    EXPECT_EQ(rebuilt_snapshot.job_attributions, report.telemetry.job_attributions);
    EXPECT_EQ(serialize_summary_json(rebuilt_snapshot, metadata(scenario, report)),
              serialize_summary_json(report.telemetry, metadata(scenario, report)));
}

[[nodiscard]] workload::TrainingReport run_terminal_scenario(std::string_view control) {
    const std::string yaml =
        "version: 1\ntelemetry: {mode: summary}\njobs: [{workers: [0], compute_ns: 100}]\n" +
        std::string{control};
    return workload::run_training(workload::parse_scenario(yaml));
}

void expect_terminal_metrics(const workload::TrainingReport& report, workload::JobState state) {
    ASSERT_EQ(report.jobs.size(), 1U);
    ASSERT_EQ(report.jobs.front().state, state);
    EXPECT_EQ(required_metric(report.telemetry, MetricId::JobCompletionTimeNs).histogram_count, 1U);
    EXPECT_EQ(required_metric(report.telemetry, MetricId::JobTerminalTotal,
                              {{MetricLabel::Outcome, static_cast<std::uint64_t>(state)}})
                  .scalar,
              1U);
    ASSERT_EQ(report.telemetry.job_attributions.size(), 1U);
    EXPECT_TRUE(report.telemetry.job_attributions.front().terminal);
}

TEST(TelemetryValidationTest, CompletionMetricsCoverSuccessFailureCancellationAndUnfinishedJobs) {
    expect_terminal_metrics(run_terminal_scenario(""), workload::JobState::Succeeded);
    expect_terminal_metrics(
        run_terminal_scenario("controls: [{job: 0, kind: worker_failure, at_ns: 50}]\n"),
        workload::JobState::Failed);
    expect_terminal_metrics(
        run_terminal_scenario("controls: [{job: 0, kind: cancel, at_ns: 50}]\n"),
        workload::JobState::Cancelled);

    const auto unfinished = workload::run_training(workload::parse_scenario(
        "version: 1\ntelemetry: {mode: summary}\nscheduling_policy: first-fit\n"
        "gpu_controls: [{gpu: 0, state: down, at_ns: 0}]\n"
        "jobs: [{requested_workers: 64, compute_ns: 100}]\n"));
    ASSERT_EQ(unfinished.jobs.size(), 1U);
    EXPECT_EQ(unfinished.jobs.front().state, workload::JobState::Waiting);
    EXPECT_THAT(unfinished.telemetry.metrics,
                ::testing::Not(::testing::Contains(::testing::Field(
                    &MetricSeriesSnapshot::metric, MetricId::JobCompletionTimeNs))));
    ASSERT_EQ(unfinished.telemetry.job_attributions.size(), 1U);
    EXPECT_FALSE(unfinished.telemetry.job_attributions.front().terminal);
}

} // namespace
} // namespace nexuslab::telemetry
