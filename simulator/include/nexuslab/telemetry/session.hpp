// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/telemetry/samples.hpp"
#include "nexuslab/telemetry/summary.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nexuslab::telemetry {

class TelemetrySession;

struct TelemetrySnapshot final {
    TelemetryConfiguration configuration;
    std::vector<MetricSeriesSnapshot> metrics;
    std::vector<JobAttributionSnapshot> job_attributions;
    std::vector<TelemetryRecord> records;
    std::vector<MetricSample> samples;
    std::size_t retained_correlation_edges{0};
    bool finalized{false};

    bool operator==(const TelemetrySnapshot&) const = default;
};

class TelemetrySink final {
  public:
    TelemetrySink() = default;

    [[nodiscard]] bool enabled() const noexcept;
    void record(sim::SimTimeNs timestamp, Correlation correlation,
                TelemetryObservation observation) const;

  private:
    friend class TelemetrySession;
    explicit TelemetrySink(TelemetrySession& session) noexcept;

    TelemetrySession* session_{nullptr};
};

class TelemetrySession final {
  public:
    explicit TelemetrySession(TelemetryConfiguration configuration = {});
    ~TelemetrySession();
    TelemetrySession(const TelemetrySession&) = delete;
    TelemetrySession& operator=(const TelemetrySession&) = delete;
    TelemetrySession(TelemetrySession&&) = delete;
    TelemetrySession& operator=(TelemetrySession&&) = delete;

    [[nodiscard]] TelemetryMode mode() const noexcept;
    [[nodiscard]] const TelemetryConfiguration& configuration() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] TelemetrySink sink() noexcept;

    void record(sim::SimTimeNs timestamp, Correlation correlation,
                TelemetryObservation observation);
    void finalize(sim::SimTimeNs timestamp);

    [[nodiscard]] std::optional<MetricSeriesSnapshot>
    find_metric(MetricId metric, const MetricLabels& labels = {}) const;
    [[nodiscard]] std::vector<MetricSeriesSnapshot> metric_snapshots() const;
    [[nodiscard]] std::vector<JobAttributionSnapshot> job_attributions() const;
    [[nodiscard]] std::span<const TelemetryRecord> records() const noexcept;
    [[nodiscard]] std::span<const MetricSample> samples() const noexcept;
    [[nodiscard]] std::size_t retained_correlation_edges() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] TelemetrySnapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace nexuslab::telemetry
