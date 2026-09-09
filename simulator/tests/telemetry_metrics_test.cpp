// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/metrics.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace nexuslab::telemetry {
namespace {

using ::testing::ElementsAre;

template <typename Value> [[nodiscard]] Value required(const std::optional<Value>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"required telemetry test value is absent"};
    }
    return *value;
}

TEST(TelemetryConfigurationTest, UsesSummaryDefaultsAndRejectsInvalidLimits) {
    const TelemetryConfiguration defaults;

    EXPECT_EQ(defaults.mode, TelemetryMode::Summary);
    EXPECT_EQ(mode_name(defaults.mode), std::string_view{"summary"});
    EXPECT_NO_THROW(validate_configuration(defaults));

    auto invalid = defaults;
    invalid.sample_interval_ns = 0;
    EXPECT_THROW(validate_configuration(invalid), std::invalid_argument);
    invalid = defaults;
    invalid.limits.metric_series = 0;
    EXPECT_THROW(validate_configuration(invalid), std::invalid_argument);
}

TEST(TelemetryCatalogTest, DefinesStableExplicitKindsUnitsAndBoundaries) {
    validate_metric_catalog(metric_catalog(), TelemetryLimits{}.histogram_boundaries);

    const auto& completion = metric_definition(MetricId::JobCompletionTimeNs);
    EXPECT_EQ(completion.name, std::string_view{"job_completion_time_ns"});
    EXPECT_EQ(kind_name(completion.kind), std::string_view{"histogram"});
    EXPECT_EQ(unit_name(completion.unit), std::string_view{"nanoseconds"});
    EXPECT_THAT(completion.histogram_boundaries,
                ElementsAre(1'000, 10'000, 100'000, 1'000'000, 10'000'000, 100'000'000,
                            1'000'000'000, 10'000'000'000, 60'000'000'000));
}

TEST(TelemetryCatalogTest, RejectsDuplicateNamesLabelsAndInvalidHistogramDefinitions) {
    constexpr std::array<std::uint64_t, 2> duplicate_bounds{10, 10};
    const std::array invalid_histogram{
        MetricDefinition{MetricId::JobCompletionTimeNs, "valid_name", "description",
                         MetricKind::Histogram, MetricUnit::Nanoseconds, 0, 0, duplicate_bounds},
    };
    EXPECT_THROW(validate_metric_catalog(invalid_histogram, 2), std::invalid_argument);

    const std::array duplicate_name{
        MetricDefinition{MetricId::SimulationDispatchedEvents, "same_name", "first",
                         MetricKind::Counter, MetricUnit::Count},
        MetricDefinition{MetricId::SimulationCancelledEvents, "same_name", "second",
                         MetricKind::Counter, MetricUnit::Count},
    };
    EXPECT_THROW(validate_metric_catalog(duplicate_name, 2), std::invalid_argument);

    EXPECT_THROW((MetricLabels{{MetricLabel::Job, 1}, {MetricLabel::Job, 2}}),
                 std::invalid_argument);
}

TEST(TelemetryRegistryTest, UpdatesCountersAndGaugesInCanonicalSeriesOrder) {
    MetricRegistry registry;
    const MetricLabels reverse_labels{{MetricLabel::Direction, 1}, {MetricLabel::Link, 7}};
    const MetricLabels canonical_labels{{MetricLabel::Link, 7}, {MetricLabel::Direction, 1}};

    registry.increment(MetricId::LinkAcceptedBytes, reverse_labels, 100);
    registry.increment(MetricId::LinkAcceptedBytes, canonical_labels, 25);
    registry.set_gauge(MetricId::LinkQueueWaitingBytes, canonical_labels, 80);
    registry.set_gauge(MetricId::LinkQueueWaitingBytes, canonical_labels, 20);

    EXPECT_EQ(registry.size(), 2U);
    EXPECT_EQ(required(registry.find(MetricId::LinkAcceptedBytes, canonical_labels)).scalar, 125U);
    EXPECT_EQ(required(registry.find(MetricId::LinkQueueWaitingBytes, reverse_labels)).scalar, 20U);
    EXPECT_THAT(
        registry.snapshots(),
        ElementsAre(
            MetricSeriesSnapshot{
                MetricId::LinkAcceptedBytes, canonical_labels, MetricKind::Counter, 125, {}, 0, 0},
            MetricSeriesSnapshot{MetricId::LinkQueueWaitingBytes,
                                 canonical_labels,
                                 MetricKind::Gauge,
                                 20,
                                 {},
                                 0,
                                 0}));
}

TEST(TelemetryRegistryTest, UsesInclusiveHistogramBoundsAndAnOverflowBucket) {
    MetricRegistry registry;

    registry.observe(MetricId::JobCompletionTimeNs, {}, 999);
    registry.observe(MetricId::JobCompletionTimeNs, {}, 1'000);
    registry.observe(MetricId::JobCompletionTimeNs, {}, 1'001);
    registry.observe(MetricId::JobCompletionTimeNs, {}, 60'000'000'001ULL);

    const auto snapshot = required(registry.find(MetricId::JobCompletionTimeNs));
    EXPECT_EQ(snapshot.histogram_count, 4U);
    EXPECT_EQ(snapshot.histogram_sum, 60'000'003'001ULL);
    ASSERT_EQ(snapshot.histogram_buckets.size(), 10U);
    EXPECT_EQ(snapshot.histogram_buckets[0], 2U);
    EXPECT_EQ(snapshot.histogram_buckets[1], 1U);
    EXPECT_EQ(snapshot.histogram_buckets.back(), 1U);
}

TEST(TelemetryRegistryTest, RejectsWrongOperationsMissingLabelsAndSeriesExhaustion) {
    MetricRegistry registry{1};

    EXPECT_THROW(registry.increment(MetricId::LinkAcceptedBytes), std::invalid_argument);
    EXPECT_THROW(registry.set_gauge(MetricId::SimulationDispatchedEvents, {}, 1),
                 std::invalid_argument);
    EXPECT_THROW(registry.increment(MetricId::LinkUtilizationPpm,
                                    {{MetricLabel::Link, 0}, {MetricLabel::Direction, 0}}),
                 std::invalid_argument);

    registry.increment(MetricId::SimulationDispatchedEvents);
    EXPECT_THROW(registry.increment(MetricId::SimulationCancelledEvents), std::length_error);
    EXPECT_FALSE(registry.find(MetricId::SimulationCancelledEvents).has_value());
}

TEST(TelemetryRegistryTest, DetectsCounterAndHistogramOverflowWithoutPartialMutation) {
    MetricRegistry registry;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    registry.increment(MetricId::SimulationDispatchedEvents, {}, maximum);
    EXPECT_THROW(registry.increment(MetricId::SimulationDispatchedEvents), std::overflow_error);
    EXPECT_EQ(required(registry.find(MetricId::SimulationDispatchedEvents)).scalar, maximum);

    registry.observe(MetricId::JobCompletionTimeNs, {}, maximum);
    EXPECT_THROW(registry.observe(MetricId::JobCompletionTimeNs, {}, 1), std::overflow_error);
    const auto histogram = required(registry.find(MetricId::JobCompletionTimeNs));
    EXPECT_EQ(histogram.histogram_count, 1U);
    EXPECT_EQ(histogram.histogram_sum, maximum);
    EXPECT_EQ(histogram.histogram_buckets.back(), 1U);
}

TEST(TelemetryRegistryTest, PreflightValidationNeverCreatesOrUpdatesSeries) {
    MetricRegistry registry;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    EXPECT_NO_THROW(registry.validate_increment(MetricId::SimulationDispatchedEvents, {}, maximum));
    EXPECT_NO_THROW(registry.validate_set_gauge(MetricId::SimulationFinalTimeNs, {}, 12));
    EXPECT_NO_THROW(registry.validate_observe(MetricId::JobCompletionTimeNs, {}, maximum));
    EXPECT_EQ(registry.size(), 0U);

    registry.increment(MetricId::SimulationDispatchedEvents, {}, maximum);
    EXPECT_THROW(registry.validate_increment(MetricId::SimulationDispatchedEvents),
                 std::overflow_error);
    EXPECT_EQ(required(registry.find(MetricId::SimulationDispatchedEvents)).scalar, maximum);
}

TEST(TelemetryRegistryTest, SeparateRunRegistriesStartEmpty) {
    MetricRegistry first;
    first.increment(MetricId::SimulationDispatchedEvents, {}, 9);
    MetricRegistry second;

    EXPECT_EQ(first.size(), 1U);
    EXPECT_EQ(second.size(), 0U);
    EXPECT_FALSE(second.find(MetricId::SimulationDispatchedEvents).has_value());
}

} // namespace
} // namespace nexuslab::telemetry
