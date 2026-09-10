// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/records.hpp"

#include <type_traits>

namespace nexuslab::telemetry {

std::size_t correlation_edge_count(const Correlation& correlation) noexcept {
    return static_cast<std::size_t>(correlation.gpu.has_value()) +
           static_cast<std::size_t>(correlation.nic.has_value()) +
           static_cast<std::size_t>(correlation.switch_entity.has_value()) +
           static_cast<std::size_t>(correlation.port.has_value()) +
           static_cast<std::size_t>(correlation.rack.has_value()) +
           static_cast<std::size_t>(correlation.link.has_value()) +
           static_cast<std::size_t>(correlation.event.has_value()) +
           static_cast<std::size_t>(correlation.cause.has_value()) +
           static_cast<std::size_t>(correlation.job.has_value()) +
           static_cast<std::size_t>(correlation.collective.has_value()) +
           static_cast<std::size_t>(correlation.transfer.has_value()) +
           static_cast<std::size_t>(correlation.chunk.has_value()) +
           static_cast<std::size_t>(correlation.routing_decision.has_value()) +
           static_cast<std::size_t>(correlation.placement_decision.has_value()) +
           static_cast<std::size_t>(correlation.failure.has_value());
}

TelemetryRecordKind record_kind(const TelemetryObservation& observation) {
    return std::visit(
        [](const auto& value) {
            using Observation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Observation, CounterObservation>) {
                return TelemetryRecordKind::Counter;
            } else if constexpr (std::is_same_v<Observation, GaugeObservation>) {
                return TelemetryRecordKind::Gauge;
            } else if constexpr (std::is_same_v<Observation, HistogramObservation>) {
                return TelemetryRecordKind::Histogram;
            } else if constexpr (std::is_same_v<Observation, SimulationObservation>) {
                return TelemetryRecordKind::Simulation;
            } else if constexpr (std::is_same_v<Observation, JobObservation>) {
                return TelemetryRecordKind::Job;
            } else if constexpr (std::is_same_v<Observation, CollectiveObservation>) {
                return TelemetryRecordKind::Collective;
            } else if constexpr (std::is_same_v<Observation, TransferObservation>) {
                return TelemetryRecordKind::Transfer;
            } else if constexpr (std::is_same_v<Observation, QueueObservation>) {
                return TelemetryRecordKind::Queue;
            } else if constexpr (std::is_same_v<Observation, FailureObservation>) {
                return TelemetryRecordKind::Failure;
            } else if constexpr (std::is_same_v<Observation, RoutingDecisionObservation>) {
                return TelemetryRecordKind::RoutingDecision;
            } else {
                static_assert(std::is_same_v<Observation, PlacementDecisionObservation>);
                return TelemetryRecordKind::PlacementDecision;
            }
        },
        observation);
}

bool is_metric_observation(const TelemetryObservation& observation) noexcept {
    return std::holds_alternative<CounterObservation>(observation) ||
           std::holds_alternative<GaugeObservation>(observation) ||
           std::holds_alternative<HistogramObservation>(observation);
}

bool is_decision_observation(const TelemetryObservation& observation) noexcept {
    return std::holds_alternative<RoutingDecisionObservation>(observation) ||
           std::holds_alternative<PlacementDecisionObservation>(observation);
}

} // namespace nexuslab::telemetry
