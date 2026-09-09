// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/records.hpp"

#include <type_traits>

namespace nexuslab::telemetry {

std::size_t correlation_edge_count(const Correlation& correlation) noexcept {
    return static_cast<std::size_t>(correlation.link.has_value()) +
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

TelemetryRecordKind record_kind(const MetricObservation& observation) {
    return std::visit(
        [](const auto& value) {
            using Observation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Observation, CounterObservation>) {
                return TelemetryRecordKind::Counter;
            } else if constexpr (std::is_same_v<Observation, GaugeObservation>) {
                return TelemetryRecordKind::Gauge;
            } else {
                static_assert(std::is_same_v<Observation, HistogramObservation>);
                return TelemetryRecordKind::Histogram;
            }
        },
        observation);
}

} // namespace nexuslab::telemetry
