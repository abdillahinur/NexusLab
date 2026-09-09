// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/session.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace nexuslab::telemetry {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

template <typename Value> [[nodiscard]] Value required(const std::optional<Value>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"required telemetry test value is absent"};
    }
    return *value;
}

[[nodiscard]] CounterObservation dispatched(std::uint64_t amount) {
    return CounterObservation{MetricId::SimulationDispatchedEvents, {}, amount};
}

[[nodiscard]] GaugeObservation final_time(std::uint64_t value) {
    return GaugeObservation{MetricId::SimulationFinalTimeNs, {}, value};
}

[[nodiscard]] MetricSample counter_sample(std::uint64_t value, std::uint64_t timestamp) {
    return MetricSample{MetricId::SimulationDispatchedEvents,
                        {},
                        MetricKind::Counter,
                        value,
                        sim::SimTimeNs{timestamp}};
}

[[nodiscard]] MetricSample gauge_sample(std::uint64_t value, std::uint64_t timestamp) {
    return MetricSample{
        MetricId::SimulationFinalTimeNs, {}, MetricKind::Gauge, value, sim::SimTimeNs{timestamp}};
}

TEST(TelemetrySamplingTest, DefersEqualTimeBoundaryUntilTimestampIsComplete) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 10;
    TelemetrySession session{configuration};

    session.record(sim::SimTimeNs{0}, {}, dispatched(2));
    session.record(sim::SimTimeNs{10}, {}, dispatched(3));
    session.record(sim::SimTimeNs{10}, {}, final_time(10));
    EXPECT_THAT(session.samples(), IsEmpty());

    session.record(sim::SimTimeNs{11}, {}, dispatched(1));

    EXPECT_THAT(session.samples(), ElementsAre(counter_sample(5, 10), gauge_sample(10, 10)));
}

TEST(TelemetrySamplingTest, EmitsElapsedBoundariesFromPriorStableState) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 10;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{4}, {}, dispatched(7));

    session.record(sim::SimTimeNs{31}, {}, dispatched(1));

    EXPECT_THAT(session.samples(),
                ElementsAre(counter_sample(7, 10), counter_sample(7, 20), counter_sample(7, 30)));
}

TEST(TelemetrySamplingTest, FinalizationFlushesEligibleBoundaryAndSealsSession) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    configuration.sample_interval_ns = 5;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{5}, {}, dispatched(4));

    session.finalize(sim::SimTimeNs{10});

    EXPECT_TRUE(session.finalized());
    EXPECT_THAT(session.samples(), ElementsAre(counter_sample(4, 5), counter_sample(4, 10)));
    EXPECT_THROW(session.record(sim::SimTimeNs{11}, {}, dispatched(1)), std::logic_error);
    EXPECT_THROW(session.finalize(sim::SimTimeNs{10}), std::logic_error);
}

TEST(TelemetrySamplingTest, SummaryAndOffModesRetainNoSamples) {
    for (const TelemetryMode mode : {TelemetryMode::Off, TelemetryMode::Summary}) {
        TelemetryConfiguration configuration;
        configuration.mode = mode;
        configuration.sample_interval_ns = 1;
        TelemetrySession session{configuration};
        session.record(sim::SimTimeNs{0}, {}, dispatched(1));
        session.finalize(sim::SimTimeNs{3});
        EXPECT_THAT(session.samples(), IsEmpty());
    }
}

TEST(TelemetrySamplingTest, SampleLimitFailsAtWholeFrameAndPreservesPrefix) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 10;
    configuration.limits.samples = 3;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{0}, {}, dispatched(2));
    session.record(sim::SimTimeNs{0}, {}, final_time(0));

    EXPECT_THROW(session.record(sim::SimTimeNs{21}, {}, dispatched(1)), std::length_error);

    EXPECT_THAT(session.samples(), ElementsAre(counter_sample(2, 10), gauge_sample(0, 10)));
    EXPECT_EQ(session.records().size(), 0U);
}

TEST(TelemetrySamplingTest, RejectedLaterObservationDoesNotAdvanceSamplingTime) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 10;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{0}, {}, dispatched(2));

    EXPECT_THROW(session.record(sim::SimTimeNs{100}, {},
                                CounterObservation{MetricId::LinkAcceptedBytes, {}, 1}),
                 std::invalid_argument);
    session.record(sim::SimTimeNs{5}, {}, dispatched(3));
    session.record(sim::SimTimeNs{11}, {}, final_time(11));

    EXPECT_THAT(session.samples(), ElementsAre(counter_sample(5, 10)));
}

TEST(TelemetrySamplingTest, HistogramsRemainInSummaryButAreNotPeriodicScalarSamples) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 10;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{0}, {},
                   HistogramObservation{MetricId::JobCompletionTimeNs, {}, 8});

    session.finalize(sim::SimTimeNs{10});

    EXPECT_THAT(session.samples(), IsEmpty());
    EXPECT_EQ(required(session.find_metric(MetricId::JobCompletionTimeNs)).histogram_count, 1U);
}

TEST(TelemetrySamplingTest, EmptyScalarSeriesFastForwardAcrossMaximumTimeRange) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 1;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{0}, {},
                   HistogramObservation{MetricId::JobCompletionTimeNs, {}, 8});

    session.finalize(sim::SimTimeNs{std::numeric_limits<std::uint64_t>::max()});

    EXPECT_THAT(session.samples(), IsEmpty());
}

TEST(TelemetrySamplingTest, NearMaximumBoundaryDoesNotWrap) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = std::numeric_limits<std::uint64_t>::max();
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{0}, {}, dispatched(1));

    session.finalize(sim::SimTimeNs{std::numeric_limits<std::uint64_t>::max()});

    EXPECT_THAT(session.samples(),
                ElementsAre(counter_sample(1, std::numeric_limits<std::uint64_t>::max())));
}

TEST(TelemetrySamplingTest, RegressingFinalTimestampDoesNotSealSession) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Sampled;
    configuration.sample_interval_ns = 10;
    TelemetrySession session{configuration};
    session.record(sim::SimTimeNs{15}, {}, dispatched(1));

    EXPECT_THROW(session.finalize(sim::SimTimeNs{14}), std::invalid_argument);

    EXPECT_FALSE(session.finalized());
    session.finalize(sim::SimTimeNs{20});
    EXPECT_THAT(session.samples(), ElementsAre(counter_sample(1, 20)));
}

} // namespace
} // namespace nexuslab::telemetry
