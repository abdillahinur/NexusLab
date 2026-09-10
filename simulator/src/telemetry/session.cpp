// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/session.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace nexuslab::telemetry {
namespace {

[[nodiscard]] std::size_t checked_add_size(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::overflow_error{"telemetry correlation edge overflow"};
    }
    return left + right;
}

void apply_observation(MetricRegistry& metrics, const TelemetryObservation& observation) {
    std::visit(
        [&metrics](const auto& value) {
            using Observation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Observation, CounterObservation>) {
                metrics.increment(value.metric, value.labels, value.amount);
            } else if constexpr (std::is_same_v<Observation, GaugeObservation>) {
                metrics.set_gauge(value.metric, value.labels, value.value);
            } else if constexpr (std::is_same_v<Observation, HistogramObservation>) {
                metrics.observe(value.metric, value.labels, value.value);
            }
        },
        observation);
}

void validate_observation(const MetricRegistry& metrics, const TelemetryObservation& observation) {
    std::visit(
        [&metrics](const auto& value) {
            using Observation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Observation, CounterObservation>) {
                metrics.validate_increment(value.metric, value.labels, value.amount);
            } else if constexpr (std::is_same_v<Observation, GaugeObservation>) {
                metrics.validate_set_gauge(value.metric, value.labels, value.value);
            } else if constexpr (std::is_same_v<Observation, HistogramObservation>) {
                metrics.validate_observe(value.metric, value.labels, value.value);
            }
        },
        observation);
}

} // namespace

struct TelemetrySession::Impl final {
    explicit Impl(TelemetryConfiguration value)
        : configuration{value}, next_sample_timestamp{sim::SimTimeNs{value.sample_interval_ns}} {
        validate_configuration(configuration);
        if (configuration.mode != TelemetryMode::Off) {
            metrics = std::make_unique<MetricRegistry>(configuration.limits.metric_series);
        }
    }

    void record(sim::SimTimeNs timestamp, Correlation correlation,
                TelemetryObservation observation) {
        if (configuration.mode == TelemetryMode::Off) {
            return;
        }
        if (is_finalized) {
            throw std::logic_error{"cannot record telemetry after finalization"};
        }
        if (last_timestamp.has_value() && timestamp < *last_timestamp) {
            throw std::invalid_argument{"telemetry observation timestamp regressed"};
        }

        const bool decision = is_decision_observation(observation);
        const bool retain = configuration.mode == TelemetryMode::Full ||
                            (configuration.mode == TelemetryMode::Sampled && decision);
        const std::size_t edges = correlation_edge_count(correlation);
        std::size_t next_edge_count = retained_edges;
        if (retain) {
            if (decision && retained_decisions >= configuration.limits.decision_records) {
                throw std::length_error{"telemetry decision record limit exceeded"};
            }
            if (!decision && retained_domain_records >= configuration.limits.domain_records) {
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

        validate_observation(*metrics, observation);
        emit_samples_until(timestamp, false);
        apply_observation(*metrics, observation);
        if (retain) {
            const TelemetryRecordId id{next_record_id};
            records.push_back(TelemetryRecord{correlation, std::move(observation), id, timestamp});
            retained_edges = next_edge_count;
            if (decision) {
                ++retained_decisions;
            } else {
                ++retained_domain_records;
            }
            if (next_record_id == std::numeric_limits<std::uint64_t>::max()) {
                record_ids_exhausted = true;
            } else {
                ++next_record_id;
            }
        }
        last_timestamp = timestamp;
    }

    void finalize(sim::SimTimeNs timestamp) {
        if (is_finalized) {
            throw std::logic_error{"telemetry session is already finalized"};
        }
        if (configuration.mode != TelemetryMode::Off && last_timestamp.has_value() &&
            timestamp < *last_timestamp) {
            throw std::invalid_argument{"telemetry final timestamp regressed"};
        }
        emit_samples_until(timestamp, true);
        last_timestamp = timestamp;
        is_finalized = true;
    }

    void emit_samples_until(sim::SimTimeNs timestamp, bool inclusive) {
        if (!retains_samples()) {
            return;
        }
        const std::vector<MetricSeriesSnapshot> snapshots = metrics->snapshots();
        const bool has_sampled_series =
            std::any_of(snapshots.begin(), snapshots.end(), [](const MetricSeriesSnapshot& sample) {
                return is_sampled_metric_kind(sample.kind);
            });
        if (!has_sampled_series) {
            skip_empty_sample_boundaries(timestamp, inclusive);
            return;
        }
        while (next_sample_timestamp.has_value() &&
               (*next_sample_timestamp < timestamp ||
                (inclusive && *next_sample_timestamp == timestamp))) {
            append_sample_frame(*next_sample_timestamp);
            advance_sample_timestamp(*next_sample_timestamp);
        }
    }

    void skip_empty_sample_boundaries(sim::SimTimeNs timestamp, bool inclusive) {
        if (!next_sample_timestamp.has_value() || (!inclusive && timestamp.count() == 0)) {
            return;
        }
        const std::uint64_t last_eligible = inclusive ? timestamp.count() : timestamp.count() - 1U;
        const std::uint64_t first = next_sample_timestamp->count();
        if (first > last_eligible) {
            return;
        }
        const std::uint64_t complete_intervals =
            (last_eligible - first) / configuration.sample_interval_ns;
        const std::uint64_t last_boundary =
            first + (complete_intervals * configuration.sample_interval_ns);
        if (configuration.sample_interval_ns >
            std::numeric_limits<std::uint64_t>::max() - last_boundary) {
            next_sample_timestamp.reset();
        } else {
            next_sample_timestamp =
                sim::SimTimeNs{last_boundary + configuration.sample_interval_ns};
        }
    }

    void append_sample_frame(sim::SimTimeNs timestamp) {
        const std::vector<MetricSeriesSnapshot> snapshots = metrics->snapshots();
        const auto sample_count = static_cast<std::size_t>(std::count_if(
            snapshots.begin(), snapshots.end(), [](const MetricSeriesSnapshot& snapshot) {
                return is_sampled_metric_kind(snapshot.kind);
            }));
        if (sample_count > configuration.limits.samples - samples.size()) {
            throw std::length_error{"telemetry sample limit exceeded"};
        }

        samples.reserve(samples.size() + sample_count);
        for (const MetricSeriesSnapshot& snapshot : snapshots) {
            if (is_sampled_metric_kind(snapshot.kind)) {
                samples.push_back(MetricSample{snapshot.metric, snapshot.labels, snapshot.kind,
                                               snapshot.scalar, timestamp});
            }
        }
    }

    void advance_sample_timestamp(sim::SimTimeNs current_timestamp) {
        const std::uint64_t current = current_timestamp.count();
        if (configuration.sample_interval_ns >
            std::numeric_limits<std::uint64_t>::max() - current) {
            next_sample_timestamp.reset();
            return;
        }
        next_sample_timestamp = sim::SimTimeNs{current + configuration.sample_interval_ns};
    }

    [[nodiscard]] bool retains_samples() const noexcept {
        return configuration.mode == TelemetryMode::Sampled ||
               configuration.mode == TelemetryMode::Full;
    }

    TelemetryConfiguration configuration;
    std::unique_ptr<MetricRegistry> metrics;
    std::vector<TelemetryRecord> records;
    std::vector<MetricSample> samples;
    std::optional<sim::SimTimeNs> last_timestamp;
    std::optional<sim::SimTimeNs> next_sample_timestamp;
    std::size_t retained_edges{0};
    std::size_t retained_decisions{0};
    std::size_t retained_domain_records{0};
    std::uint64_t next_record_id{0};
    bool record_ids_exhausted{false};
    bool is_finalized{false};
};

TelemetrySink::TelemetrySink(TelemetrySession& session) noexcept : session_{&session} {}

bool TelemetrySink::enabled() const noexcept { return session_ != nullptr && session_->enabled(); }

void TelemetrySink::record(sim::SimTimeNs timestamp, Correlation correlation,
                           TelemetryObservation observation) const {
    if (enabled()) {
        session_->record(timestamp, correlation, std::move(observation));
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
                              TelemetryObservation observation) {
    implementation_->record(timestamp, correlation, std::move(observation));
}

void TelemetrySession::finalize(sim::SimTimeNs timestamp) { implementation_->finalize(timestamp); }

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

std::span<const MetricSample> TelemetrySession::samples() const noexcept {
    return implementation_->samples;
}

std::size_t TelemetrySession::retained_correlation_edges() const noexcept {
    return implementation_->retained_edges;
}

bool TelemetrySession::finalized() const noexcept { return implementation_->is_finalized; }

} // namespace nexuslab::telemetry
