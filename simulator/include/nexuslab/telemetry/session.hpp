// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/telemetry/records.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nexuslab::telemetry {

class TelemetrySession;

class TelemetrySink final {
  public:
    TelemetrySink() = default;

    [[nodiscard]] bool enabled() const noexcept;
    void record(sim::SimTimeNs timestamp, Correlation correlation,
                MetricObservation observation) const;

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
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] TelemetrySink sink() noexcept;

    void record(sim::SimTimeNs timestamp, Correlation correlation, MetricObservation observation);

    [[nodiscard]] std::optional<MetricSeriesSnapshot>
    find_metric(MetricId metric, const MetricLabels& labels = {}) const;
    [[nodiscard]] std::vector<MetricSeriesSnapshot> metric_snapshots() const;
    [[nodiscard]] std::span<const TelemetryRecord> records() const noexcept;
    [[nodiscard]] std::size_t retained_correlation_edges() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace nexuslab::telemetry
