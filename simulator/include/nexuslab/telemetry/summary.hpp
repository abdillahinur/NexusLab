// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/telemetry/records.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace nexuslab::telemetry {

struct JobAttributionSnapshot final {
    workload::JobId job;
    std::uint64_t scheduling_wait_ns{0};
    std::uint64_t compute_ns{0};
    std::uint64_t communication_ns{0};
    std::uint64_t synchronization_wait_ns{0};
    std::uint64_t straggler_delay_ns{0};
    std::uint64_t terminal_other_ns{0};
    bool terminal{false};

    [[nodiscard]] std::uint64_t accounted_ns() const;
    bool operator==(const JobAttributionSnapshot&) const = default;
};

class SummaryBuilder final {
  public:
    explicit SummaryBuilder(std::size_t maximum_metric_series = TelemetryLimits{}.metric_series);

    void validate(sim::SimTimeNs timestamp, const Correlation& correlation,
                  const TelemetryObservation& observation) const;
    void consume(sim::SimTimeNs timestamp, const Correlation& correlation,
                 const TelemetryObservation& observation);
    void finalize(sim::SimTimeNs timestamp);

    [[nodiscard]] std::optional<MetricSeriesSnapshot>
    find_metric(MetricId metric, const MetricLabels& labels = {}) const;
    [[nodiscard]] std::vector<MetricSeriesSnapshot> metric_snapshots() const;
    [[nodiscard]] std::vector<JobAttributionSnapshot> job_attributions() const;
    [[nodiscard]] bool finalized() const noexcept;

  private:
    enum class JobPhase : std::uint8_t {
        SchedulingWait,
        Compute,
        Communication,
        SynchronizationWait,
        StragglerDelay,
        TerminalOther,
        Terminal,
    };

    struct JobState final {
        JobAttributionSnapshot snapshot;
        sim::SimTimeNs last_transition;
        JobPhase phase{JobPhase::SchedulingWait};
    };

    [[nodiscard]] static JobState transition(const std::optional<JobState>& current,
                                             workload::JobId job, sim::SimTimeNs timestamp,
                                             JobTransition transition, bool collective_active);
    void validate_terminal_metrics(const JobState& state) const;
    void apply_terminal_metrics(const JobState& state);
    [[nodiscard]] static bool terminal_transition(JobTransition transition) noexcept;
    [[nodiscard]] static JobPhase next_phase(JobPhase current, JobTransition transition,
                                             bool collective_active) noexcept;
    static void add_interval(JobAttributionSnapshot& snapshot, JobPhase phase,
                             std::uint64_t duration);

    MetricRegistry metrics_;
    std::map<workload::JobId, JobState> jobs_;
    std::optional<sim::SimTimeNs> last_timestamp_;
    std::size_t maximum_metric_series_;
    bool finalized_{false};
};

} // namespace nexuslab::telemetry
