// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/session.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <vector>

namespace nexuslab::telemetry {
namespace {

using ::testing::IsEmpty;

template <typename Value> [[nodiscard]] Value required(const std::optional<Value>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"required telemetry test value is absent"};
    }
    return *value;
}

[[nodiscard]] CounterObservation dispatched(std::uint64_t amount = 1) {
    return CounterObservation{MetricId::SimulationDispatchedEvents, {}, amount};
}

void expect_disabled_mode() {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Off;
    TelemetrySession session{configuration};

    session.record(sim::SimTimeNs{10}, {}, dispatched(3));

    EXPECT_FALSE(session.enabled());
    EXPECT_THAT(session.metric_snapshots(), IsEmpty());
    EXPECT_THAT(session.records(), IsEmpty());
}

void expect_enabled_mode(TelemetryMode mode, std::size_t expected_records) {
    TelemetryConfiguration configuration;
    configuration.mode = mode;
    TelemetrySession session{configuration};

    session.record(sim::SimTimeNs{10}, {}, dispatched(3));

    EXPECT_TRUE(session.enabled());
    EXPECT_EQ(required(session.find_metric(MetricId::SimulationDispatchedEvents)).scalar, 3U);
    EXPECT_EQ(session.records().size(), expected_records);
}

TEST(TelemetrySessionTest, ModesControlAggregationAndFullRecordRetention) {
    expect_disabled_mode();
    expect_enabled_mode(TelemetryMode::Summary, 0U);
    expect_enabled_mode(TelemetryMode::Sampled, 0U);
    expect_enabled_mode(TelemetryMode::Full, 1U);
}

TEST(TelemetrySessionTest, DisabledSinkDoesNotInspectInvalidObservations) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Off;
    TelemetrySession session{configuration};
    TelemetrySink absent;

    EXPECT_FALSE(absent.enabled());
    EXPECT_FALSE(session.sink().enabled());
    EXPECT_NO_THROW(absent.record(sim::SimTimeNs{10}, {},
                                  CounterObservation{MetricId::LinkAcceptedBytes, {}, 1}));
    EXPECT_NO_THROW(session.sink().record(
        sim::SimTimeNs{5}, {}, GaugeObservation{MetricId::SimulationDispatchedEvents, {}, 1}));
    EXPECT_THAT(session.metric_snapshots(), IsEmpty());
}

TEST(TelemetrySessionTest, FullRecordsUseStableOrderAndTypedCorrelations) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession session{configuration};
    Correlation first;
    first.event = sim::EventId{9};
    first.job = workload::JobId{2};
    first.collective = collective::CollectiveId{4};
    first.transfer = transport::TransferId{6};
    first.chunk = transport::ChunkId{8};

    session.sink().record(sim::SimTimeNs{50}, first, dispatched());
    session.sink().record(sim::SimTimeNs{50}, {},
                          GaugeObservation{MetricId::SimulationFinalTimeNs, {}, 50});

    ASSERT_EQ(session.records().size(), 2U);
    EXPECT_EQ(session.records()[0],
              (TelemetryRecord{first, dispatched(), TelemetryRecordId{0}, sim::SimTimeNs{50}}));
    EXPECT_EQ(session.records()[1].id, TelemetryRecordId{1});
    EXPECT_EQ(record_kind(session.records()[0].observation), TelemetryRecordKind::Counter);
    EXPECT_EQ(record_kind(session.records()[1].observation), TelemetryRecordKind::Gauge);
    EXPECT_EQ(session.retained_correlation_edges(), 5U);
}

TEST(TelemetrySessionTest, TimestampRegressionFailsBeforeMetricsOrRecordsChange) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{20}, {}, dispatched(2));

    EXPECT_THROW(session.record(sim::SimTimeNs{19}, {}, dispatched(10)), std::invalid_argument);

    EXPECT_EQ(required(session.find_metric(MetricId::SimulationDispatchedEvents)).scalar, 2U);
    ASSERT_EQ(session.records().size(), 1U);
    EXPECT_EQ(session.records().front().id, TelemetryRecordId{0});
}

TEST(TelemetrySessionTest, RetentionAndCorrelationLimitsFailBeforeAggregation) {
    TelemetryConfiguration record_configuration;
    record_configuration.mode = TelemetryMode::Full;
    record_configuration.limits.domain_records = 1;
    TelemetrySession record_limited{record_configuration};
    record_limited.record(sim::SimTimeNs{0}, {}, dispatched(2));
    EXPECT_THROW(record_limited.record(sim::SimTimeNs{1}, {}, dispatched(5)), std::length_error);
    EXPECT_EQ(required(record_limited.find_metric(MetricId::SimulationDispatchedEvents)).scalar,
              2U);

    TelemetryConfiguration edge_configuration;
    edge_configuration.mode = TelemetryMode::Full;
    edge_configuration.limits.correlation_edges = 1;
    TelemetrySession edge_limited{edge_configuration};
    Correlation two_edges;
    two_edges.event = sim::EventId{0};
    two_edges.job = workload::JobId{0};
    EXPECT_THROW(edge_limited.record(sim::SimTimeNs{0}, two_edges, dispatched()),
                 std::length_error);
    EXPECT_FALSE(edge_limited.find_metric(MetricId::SimulationDispatchedEvents).has_value());
    EXPECT_THAT(edge_limited.records(), IsEmpty());
}

TEST(TelemetrySessionTest, InvalidMetricUpdateDoesNotConsumeRecordIdentity) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession session{configuration};

    EXPECT_THROW(session.record(sim::SimTimeNs{0}, {},
                                CounterObservation{MetricId::LinkAcceptedBytes, {}, 1}),
                 std::invalid_argument);
    session.record(sim::SimTimeNs{0}, {}, dispatched());

    ASSERT_EQ(session.records().size(), 1U);
    EXPECT_EQ(session.records().front().id, TelemetryRecordId{0});
}

TEST(TelemetrySessionTest, NewRunStartsWithEmptyMetricsRecordsAndIds) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession first{configuration};
    first.record(sim::SimTimeNs{4}, {}, dispatched(9));
    TelemetrySession second{configuration};
    second.record(sim::SimTimeNs{1}, {}, dispatched());

    EXPECT_EQ(required(first.find_metric(MetricId::SimulationDispatchedEvents)).scalar, 9U);
    EXPECT_EQ(required(second.find_metric(MetricId::SimulationDispatchedEvents)).scalar, 1U);
    EXPECT_EQ(second.records().front().id, TelemetryRecordId{0});
}

} // namespace
} // namespace nexuslab::telemetry
