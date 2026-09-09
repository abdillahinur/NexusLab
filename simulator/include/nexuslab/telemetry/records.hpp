// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/collective/model.hpp"
#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/sim/time.hpp"
#include "nexuslab/telemetry/metrics.hpp"
#include "nexuslab/topology/entities.hpp"
#include "nexuslab/transport/types.hpp"
#include "nexuslab/workload/events.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <variant>

namespace nexuslab::telemetry {

struct TelemetryRecordIdTag final {};
struct RoutingDecisionIdTag final {};
struct PlacementDecisionIdTag final {};
struct FailureIdTag final {};

using TelemetryRecordId = topology::StrongId<TelemetryRecordIdTag>;
using RoutingDecisionId = topology::StrongId<RoutingDecisionIdTag>;
using PlacementDecisionId = topology::StrongId<PlacementDecisionIdTag>;
using FailureId = topology::StrongId<FailureIdTag>;

struct Correlation final {
    std::optional<topology::DirectedLinkId> link;
    std::optional<sim::EventId> event;
    std::optional<sim::EventId> cause;
    std::optional<workload::JobId> job;
    std::optional<collective::CollectiveId> collective;
    std::optional<transport::TransferId> transfer;
    std::optional<transport::ChunkId> chunk;
    std::optional<RoutingDecisionId> routing_decision;
    std::optional<PlacementDecisionId> placement_decision;
    std::optional<FailureId> failure;

    bool operator==(const Correlation&) const = default;
};

[[nodiscard]] std::size_t correlation_edge_count(const Correlation& correlation) noexcept;

struct CounterObservation final {
    MetricId metric;
    MetricLabels labels;
    std::uint64_t amount;

    bool operator==(const CounterObservation&) const = default;
};

struct GaugeObservation final {
    MetricId metric;
    MetricLabels labels;
    std::uint64_t value;

    bool operator==(const GaugeObservation&) const = default;
};

struct HistogramObservation final {
    MetricId metric;
    MetricLabels labels;
    std::uint64_t value;

    bool operator==(const HistogramObservation&) const = default;
};

using MetricObservation = std::variant<CounterObservation, GaugeObservation, HistogramObservation>;

enum class TelemetryRecordKind : std::uint8_t {
    Counter = 1,
    Gauge = 2,
    Histogram = 3,
};

[[nodiscard]] TelemetryRecordKind record_kind(const MetricObservation& observation);

struct TelemetryRecord final {
    Correlation correlation;
    MetricObservation observation;
    TelemetryRecordId id;
    sim::SimTimeNs timestamp;

    bool operator==(const TelemetryRecord&) const = default;
};

} // namespace nexuslab::telemetry
