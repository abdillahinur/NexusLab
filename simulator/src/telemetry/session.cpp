// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/session.hpp"

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace nexuslab::telemetry {
namespace {

[[nodiscard]] std::size_t checked_add_size(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::overflow_error{"telemetry correlation edge overflow"};
    }
    return left + right;
}

void apply_observation(MetricRegistry& metrics, const MetricObservation& observation) {
    std::visit(
        [&metrics](const auto& value) {
            using Observation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Observation, CounterObservation>) {
                metrics.increment(value.metric, value.labels, value.amount);
            } else if constexpr (std::is_same_v<Observation, GaugeObservation>) {
                metrics.set_gauge(value.metric, value.labels, value.value);
            } else {
                static_assert(std::is_same_v<Observation, HistogramObservation>);
                metrics.observe(value.metric, value.labels, value.value);
            }
        },
        observation);
}

} // namespace

struct TelemetrySession::Impl final {
    explicit Impl(TelemetryConfiguration value) : configuration{value} {
        validate_configuration(configuration);
        if (configuration.mode != TelemetryMode::Off) {
            metrics = std::make_unique<MetricRegistry>(configuration.limits.metric_series);
        }
    }

    void record(sim::SimTimeNs timestamp, Correlation correlation, MetricObservation observation) {
        if (configuration.mode == TelemetryMode::Off) {
            return;
        }
        if (last_timestamp.has_value() && timestamp < *last_timestamp) {
            throw std::invalid_argument{"telemetry observation timestamp regressed"};
        }

        const bool retain = configuration.mode == TelemetryMode::Full;
        const std::size_t edges = correlation_edge_count(correlation);
        std::size_t next_edge_count = retained_edges;
        if (retain) {
            if (records.size() >= configuration.limits.domain_records) {
                throw std::length_error{"telemetry domain record limit exceeded"};
            }
            next_edge_count = checked_add_size(retained_edges, edges);
            if (next_edge_count > configuration.limits.correlation_edges) {
                throw std::length_error{"telemetry correlation edge limit exceeded"};
            }
            if (record_ids_exhausted) {
                throw std::overflow_error{"telemetry record ID sequence exhausted"};
            }
        }

        apply_observation(*metrics, observation);
        if (retain) {
            const TelemetryRecordId id{next_record_id};
            records.push_back(TelemetryRecord{correlation, observation, id, timestamp});
            retained_edges = next_edge_count;
            if (next_record_id == std::numeric_limits<std::uint64_t>::max()) {
                record_ids_exhausted = true;
            } else {
                ++next_record_id;
            }
        }
        last_timestamp = timestamp;
    }

    TelemetryConfiguration configuration;
    std::unique_ptr<MetricRegistry> metrics;
    std::vector<TelemetryRecord> records;
    std::optional<sim::SimTimeNs> last_timestamp;
    std::size_t retained_edges{0};
    std::uint64_t next_record_id{0};
    bool record_ids_exhausted{false};
};

TelemetrySink::TelemetrySink(TelemetrySession& session) noexcept : session_{&session} {}

bool TelemetrySink::enabled() const noexcept { return session_ != nullptr && session_->enabled(); }

void TelemetrySink::record(sim::SimTimeNs timestamp, Correlation correlation,
                           MetricObservation observation) const {
    if (enabled()) {
        session_->record(timestamp, correlation, observation);
    }
}

TelemetrySession::TelemetrySession(TelemetryConfiguration configuration)
    : implementation_{std::make_unique<Impl>(configuration)} {}

TelemetrySession::~TelemetrySession() = default;

TelemetryMode TelemetrySession::mode() const noexcept {
    return implementation_->configuration.mode;
}

bool TelemetrySession::enabled() const noexcept { return mode() != TelemetryMode::Off; }

TelemetrySink TelemetrySession::sink() noexcept { return TelemetrySink{*this}; }

void TelemetrySession::record(sim::SimTimeNs timestamp, Correlation correlation,
                              MetricObservation observation) {
    implementation_->record(timestamp, correlation, observation);
}

std::optional<MetricSeriesSnapshot>
TelemetrySession::find_metric(MetricId metric, const MetricLabels& labels) const {
    if (!enabled()) {
        return std::nullopt;
    }
    return implementation_->metrics->find(metric, labels);
}

std::vector<MetricSeriesSnapshot> TelemetrySession::metric_snapshots() const {
    if (!enabled()) {
        return {};
    }
    return implementation_->metrics->snapshots();
}

std::span<const TelemetryRecord> TelemetrySession::records() const noexcept {
    return implementation_->records;
}

std::size_t TelemetrySession::retained_correlation_edges() const noexcept {
    return implementation_->retained_edges;
}

} // namespace nexuslab::telemetry
