// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/summary.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stdexcept>

namespace nexuslab::telemetry {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

[[nodiscard]] Correlation
correlated(workload::JobId job, std::optional<collective::CollectiveId> collective = std::nullopt) {
    Correlation correlation;
    correlation.job = job;
    correlation.collective = collective;
    return correlation;
}

void job_transition(SummaryBuilder& summary, workload::JobId job, std::uint64_t timestamp,
                    JobTransition transition,
                    std::optional<collective::CollectiveId> collective = std::nullopt) {
    summary.consume(sim::SimTimeNs{timestamp}, correlated(job, collective),
                    JobObservation{transition, 0, 0, 0, 2});
}

[[nodiscard]] MetricSeriesSnapshot required(const SummaryBuilder& summary, MetricId metric) {
    const auto snapshot = summary.find_metric(metric);
    if (!snapshot.has_value()) {
        throw std::logic_error{"required summary metric is absent"};
    }
    return *snapshot;
}

TEST(TelemetrySummaryTest, PartitionsJobCriticalPathIntoNonOverlappingIntervals) {
    SummaryBuilder summary;
    const workload::JobId job{7};

    job_transition(summary, job, 10, JobTransition::Arrived);
    job_transition(summary, job, 20, JobTransition::Admitted);
    job_transition(summary, job, 20, JobTransition::ComputeStarted);
    job_transition(summary, job, 50, JobTransition::StragglerStarted);
    job_transition(summary, job, 70, JobTransition::ComputeCompleted);
    job_transition(summary, job, 75, JobTransition::CollectiveStarted, collective::CollectiveId{3});
    job_transition(summary, job, 100, JobTransition::CollectiveCompleted,
                   collective::CollectiveId{3});
    job_transition(summary, job, 105, JobTransition::Succeeded);
    summary.finalize(sim::SimTimeNs{105});

    EXPECT_THAT(summary.job_attributions(),
                ElementsAre(JobAttributionSnapshot{job, 10, 30, 25, 5, 20, 5, true}));
    EXPECT_EQ(summary.job_attributions().front().accounted_ns(), 95U);
    EXPECT_EQ(required(summary, MetricId::JobCommunicationNs).scalar, 25U);
    EXPECT_EQ(required(summary, MetricId::JobSynchronizationWaitNs).scalar, 5U);
    EXPECT_EQ(required(summary, MetricId::JobStragglerDelayNs).scalar, 20U);
}

TEST(TelemetrySummaryTest, KeepsOverlappedCollectiveTimeInComputeUntilComputeCompletes) {
    SummaryBuilder summary;
    const workload::JobId job{0};

    job_transition(summary, job, 0, JobTransition::Arrived);
    job_transition(summary, job, 0, JobTransition::Admitted);
    job_transition(summary, job, 0, JobTransition::ComputeStarted);
    job_transition(summary, job, 10, JobTransition::CollectiveStarted, collective::CollectiveId{0});
    job_transition(summary, job, 20, JobTransition::StragglerStarted, collective::CollectiveId{0});
    job_transition(summary, job, 30, JobTransition::ComputeCompleted, collective::CollectiveId{0});
    job_transition(summary, job, 40, JobTransition::CollectiveCompleted,
                   collective::CollectiveId{0});
    job_transition(summary, job, 40, JobTransition::Succeeded);

    EXPECT_THAT(summary.job_attributions(),
                ElementsAre(JobAttributionSnapshot{job, 0, 20, 10, 0, 10, 0, true}));
}

TEST(TelemetrySummaryTest, FinalizationClosesAnUnfinishedIntervalWithoutInventingTerminalMetrics) {
    SummaryBuilder summary;
    const workload::JobId job{2};
    job_transition(summary, job, 5, JobTransition::Arrived);
    job_transition(summary, job, 5, JobTransition::Waiting);

    summary.finalize(sim::SimTimeNs{25});

    EXPECT_THAT(summary.job_attributions(),
                ElementsAre(JobAttributionSnapshot{job, 20, 0, 0, 0, 0, 0, false}));
    EXPECT_THAT(summary.metric_snapshots(), IsEmpty());
}

TEST(TelemetrySummaryTest, RejectsInvalidCorrelationAndTerminalSeriesExhaustionAtomically) {
    SummaryBuilder missing_correlation;
    EXPECT_THROW(missing_correlation.consume(sim::SimTimeNs{0}, {},
                                             JobObservation{JobTransition::Arrived, 0, 0, 0, 1}),
                 std::invalid_argument);

    SummaryBuilder limited{2};
    const workload::JobId job{1};
    job_transition(limited, job, 0, JobTransition::Arrived);
    EXPECT_THROW(job_transition(limited, job, 10, JobTransition::Failed), std::length_error);
    EXPECT_THAT(limited.metric_snapshots(), IsEmpty());
    EXPECT_THAT(limited.job_attributions(),
                ElementsAre(JobAttributionSnapshot{job, 0, 0, 0, 0, 0, 0, false}));
}

} // namespace
} // namespace nexuslab::telemetry
