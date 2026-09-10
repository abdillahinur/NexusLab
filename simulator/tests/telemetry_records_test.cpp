// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/session.hpp"

#include <gtest/gtest.h>

#include <array>
#include <stdexcept>

namespace nexuslab::telemetry {
namespace {

[[nodiscard]] RoutingDecisionObservation routing_decision(std::uint64_t id) {
    return RoutingDecisionObservation{
        RoutingDecisionId{id},
        "queue-aware",
        1,
        7,
        3,
        {topology::DirectedLinkId{topology::LinkId{4}, topology::LinkDirection::AToB},
         topology::DirectedLinkId{topology::LinkId{6}, topology::LinkDirection::BToA}},
        42,
        "minimum estimated delay",
        transport::TransferId{9},
        RoutingDecisionOutcome::Selected};
}

[[nodiscard]] PlacementDecisionObservation placement_decision(std::uint64_t id) {
    return PlacementDecisionObservation{
        PlacementDecisionId{id},
        "compact",
        1,
        2,
        4,
        {topology::GpuId{0}, topology::GpuId{1}, topology::GpuId{2}, topology::GpuId{3}},
        1,
        1,
        0,
        "minimum fragmentation",
        PlacementDecisionOutcome::Placed};
}

TEST(TelemetryRecordTest, AssignsStableKindsToMetricDomainAndDecisionObservations) {
    const std::array observations{
        TelemetryObservation{CounterObservation{MetricId::SimulationDispatchedEvents, {}, 1}},
        TelemetryObservation{GaugeObservation{MetricId::SimulationFinalTimeNs, {}, 1}},
        TelemetryObservation{HistogramObservation{MetricId::JobCompletionTimeNs, {}, 1}},
        TelemetryObservation{SimulationObservation{SimulationTransition::RunStarted, 0}},
        TelemetryObservation{JobObservation{JobTransition::Arrived, 0, 0, 0, 0}},
        TelemetryObservation{CollectiveObservation{CollectiveTransition::Submitted, 0, 0, 16}},
        TelemetryObservation{TransferObservation{TransferTransition::Submitted, 16, 0, 0}},
        TelemetryObservation{QueueObservation{QueueTransition::Enqueued, 16, 16, 1}},
        TelemetryObservation{FailureObservation{FailureId{0}, FailureTransition::Scheduled,
                                                FailureResourceKind::Link, 2, 0}},
        TelemetryObservation{routing_decision(0)},
        TelemetryObservation{placement_decision(0)},
    };
    const std::array expected{
        TelemetryRecordKind::Counter,
        TelemetryRecordKind::Gauge,
        TelemetryRecordKind::Histogram,
        TelemetryRecordKind::Simulation,
        TelemetryRecordKind::Job,
        TelemetryRecordKind::Collective,
        TelemetryRecordKind::Transfer,
        TelemetryRecordKind::Queue,
        TelemetryRecordKind::Failure,
        TelemetryRecordKind::RoutingDecision,
        TelemetryRecordKind::PlacementDecision,
    };

    for (std::size_t index = 0; index < observations.size(); ++index) {
        EXPECT_EQ(record_kind(observations[index]), expected[index]);
    }
    EXPECT_TRUE(is_metric_observation(observations.front()));
    EXPECT_FALSE(is_decision_observation(observations.front()));
    EXPECT_TRUE(is_decision_observation(observations.back()));
    EXPECT_FALSE(is_metric_observation(observations.back()));
}

TEST(TelemetryRecordTest, CountsEveryTypedCorrelationEdge) {
    Correlation correlation;
    correlation.gpu = topology::GpuId{0};
    correlation.nic = topology::NicId{1};
    correlation.switch_entity = topology::SwitchId{2};
    correlation.port = topology::PortId{3};
    correlation.rack = topology::RackId{4};
    correlation.link = topology::DirectedLinkId{topology::LinkId{5}, topology::LinkDirection::AToB};
    correlation.event = sim::EventId{6};
    correlation.cause = sim::EventId{7};
    correlation.job = workload::JobId{8};
    correlation.collective = collective::CollectiveId{9};
    correlation.transfer = transport::TransferId{10};
    correlation.chunk = transport::ChunkId{11};
    correlation.routing_decision = RoutingDecisionId{12};
    correlation.placement_decision = PlacementDecisionId{13};
    correlation.failure = FailureId{14};

    EXPECT_EQ(correlation_edge_count(correlation), 15U);
}

TEST(TelemetryRecordTest, SampledModeRetainsDecisionsButNotDomainOrMetricRecords) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    TelemetrySession session{configuration};

    session.record(sim::SimTimeNs{0}, {},
                   SimulationObservation{SimulationTransition::RunStarted, 0});
    session.record(sim::SimTimeNs{0}, {},
                   CounterObservation{MetricId::SimulationDispatchedEvents, {}, 1});
    session.record(sim::SimTimeNs{1}, {}, routing_decision(3));
    session.record(sim::SimTimeNs{2}, {}, placement_decision(4));

    ASSERT_EQ(session.records().size(), 2U);
    EXPECT_EQ(session.records()[0].id, TelemetryRecordId{0});
    EXPECT_EQ(session.records()[1].id, TelemetryRecordId{1});
    EXPECT_EQ(record_kind(session.records()[0].observation), TelemetryRecordKind::RoutingDecision);
    EXPECT_EQ(record_kind(session.records()[1].observation),
              TelemetryRecordKind::PlacementDecision);
}

TEST(TelemetryRecordTest, FullModeAppliesSeparateDecisionAndDomainLimits) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    configuration.limits.decision_records = 1;
    configuration.limits.domain_records = 1;
    TelemetrySession session{configuration};

    session.record(sim::SimTimeNs{0}, {},
                   SimulationObservation{SimulationTransition::RunStarted, 0});
    session.record(sim::SimTimeNs{1}, {}, routing_decision(0));

    EXPECT_THROW(
        session.record(sim::SimTimeNs{2}, {}, JobObservation{JobTransition::Arrived, 0, 0, 0, 0}),
        std::length_error);
    EXPECT_THROW(session.record(sim::SimTimeNs{2}, {}, placement_decision(0)), std::length_error);
    ASSERT_EQ(session.records().size(), 2U);
    EXPECT_EQ(session.records()[0].id, TelemetryRecordId{0});
    EXPECT_EQ(session.records()[1].id, TelemetryRecordId{1});
}

TEST(TelemetryRecordTest, RetainedDecisionOwnsPolicyReasonAndSelections) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    TelemetrySession session{configuration};
    auto decision = routing_decision(8);

    session.record(sim::SimTimeNs{3}, {}, decision);
    decision.policy = "changed";
    decision.reason = "changed";
    decision.selected_path.clear();

    const auto& retained =
        std::get<RoutingDecisionObservation>(session.records().front().observation);
    EXPECT_EQ(retained.policy, "queue-aware");
    EXPECT_EQ(retained.reason, "minimum estimated delay");
    EXPECT_EQ(retained.selected_path.size(), 2U);
}

} // namespace
} // namespace nexuslab::telemetry
