// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/summary.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace nexuslab::telemetry {
namespace {

[[nodiscard]] std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error{"telemetry attribution duration overflow"};
    }
    return left + right;
}

void validate_metric_observation(const MetricRegistry& metrics,
                                 const TelemetryObservation& observation) {
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

void apply_metric_observation(MetricRegistry& metrics, const TelemetryObservation& observation) {
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

} // namespace

std::uint64_t JobAttributionSnapshot::accounted_ns() const {
    std::uint64_t result = checked_add(scheduling_wait_ns, compute_ns);
    result = checked_add(result, communication_ns);
    result = checked_add(result, synchronization_wait_ns);
    result = checked_add(result, straggler_delay_ns);
    return checked_add(result, terminal_other_ns);
}

SummaryBuilder::SummaryBuilder(std::size_t maximum_metric_series,
                               std::size_t maximum_histogram_boundaries)
    : metrics_{maximum_metric_series, maximum_histogram_boundaries},
      maximum_metric_series_{maximum_metric_series} {}

void SummaryBuilder::validate(sim::SimTimeNs timestamp, const Correlation& correlation,
                              const TelemetryObservation& observation) const {
    if (finalized_) {
        throw std::logic_error{"cannot add to a finalized telemetry summary"};
    }
    if (last_timestamp_.has_value() && timestamp < *last_timestamp_) {
        throw std::invalid_argument{"telemetry summary timestamp regressed"};
    }
    validate_metric_observation(metrics_, observation);
    const auto* const job_observation = std::get_if<JobObservation>(&observation);
    if (job_observation == nullptr) {
        return;
    }
    if (!correlation.job.has_value()) {
        throw std::invalid_argument{"job telemetry observation requires job correlation"};
    }
    const auto found = jobs_.find(*correlation.job);
    const std::optional<JobState> current =
        found == jobs_.end() ? std::nullopt : std::optional{found->second};
    const JobState next =
        transition(current, *correlation.job, timestamp, job_observation->transition,
                   correlation.collective.has_value());
    if (next.snapshot.terminal) {
        validate_terminal_metrics(next);
    }
}

void SummaryBuilder::consume(sim::SimTimeNs timestamp, const Correlation& correlation,
                             const TelemetryObservation& observation) {
    validate(timestamp, correlation, observation);
    apply_metric_observation(metrics_, observation);
    if (const auto* const job_observation = std::get_if<JobObservation>(&observation)) {
        if (!correlation.job.has_value()) {
            throw std::logic_error{"validated job telemetry lost its correlation"};
        }
        const workload::JobId job = *correlation.job;
        const auto found = jobs_.find(job);
        const std::optional<JobState> current =
            found == jobs_.end() ? std::nullopt : std::optional{found->second};
        const JobState next = transition(current, job, timestamp, job_observation->transition,
                                         correlation.collective.has_value());
        if (next.snapshot.terminal) {
            apply_terminal_metrics(next);
        }
        jobs_.insert_or_assign(job, next);
    }
    last_timestamp_ = timestamp;
}

void SummaryBuilder::finalize(sim::SimTimeNs timestamp) {
    if (finalized_) {
        throw std::logic_error{"telemetry summary is already finalized"};
    }
    if (last_timestamp_.has_value() && timestamp < *last_timestamp_) {
        throw std::invalid_argument{"telemetry summary final timestamp regressed"};
    }
    for (auto& [job, state] : jobs_) {
        static_cast<void>(job);
        if (!state.snapshot.terminal) {
            add_interval(state.snapshot, state.phase,
                         timestamp.count() - state.last_transition.count());
            state.last_transition = timestamp;
        }
    }
    last_timestamp_ = timestamp;
    finalized_ = true;
}

std::optional<MetricSeriesSnapshot> SummaryBuilder::find_metric(MetricId metric,
                                                                const MetricLabels& labels) const {
    return metrics_.find(metric, labels);
}

std::vector<MetricSeriesSnapshot> SummaryBuilder::metric_snapshots() const {
    return metrics_.snapshots();
}

std::vector<JobAttributionSnapshot> SummaryBuilder::job_attributions() const {
    std::vector<JobAttributionSnapshot> result;
    result.reserve(jobs_.size());
    for (const auto& [job, state] : jobs_) {
        static_cast<void>(job);
        result.push_back(state.snapshot);
    }
    return result;
}

bool SummaryBuilder::finalized() const noexcept { return finalized_; }

SummaryBuilder::JobState SummaryBuilder::transition(const std::optional<JobState>& current,
                                                    workload::JobId job, sim::SimTimeNs timestamp,
                                                    JobTransition transition_value,
                                                    bool collective_active) {
    if (!current.has_value()) {
        if (transition_value != JobTransition::Arrived && !terminal_transition(transition_value)) {
            throw std::logic_error{"job attribution must begin with arrival or termination"};
        }
        JobState state{JobAttributionSnapshot{job}, timestamp, JobPhase::SchedulingWait};
        if (terminal_transition(transition_value)) {
            state.phase = JobPhase::Terminal;
            state.snapshot.terminal = true;
        }
        return state;
    }
    if (current->snapshot.terminal) {
        throw std::logic_error{"job attribution received a transition after termination"};
    }
    if (transition_value == JobTransition::Arrived) {
        throw std::logic_error{"job attribution received duplicate arrival"};
    }
    if (timestamp < current->last_transition) {
        throw std::invalid_argument{"job attribution timestamp regressed"};
    }

    JobState state = *current;
    add_interval(state.snapshot, state.phase, timestamp.count() - state.last_transition.count());
    state.last_transition = timestamp;
    state.phase = next_phase(state.phase, transition_value, collective_active);
    state.snapshot.terminal = state.phase == JobPhase::Terminal;
    return state;
}

void SummaryBuilder::validate_terminal_metrics(const JobState& state) const {
    constexpr std::array metrics{MetricId::JobCommunicationNs, MetricId::JobSynchronizationWaitNs,
                                 MetricId::JobStragglerDelayNs};
    const std::array values{state.snapshot.communication_ns, state.snapshot.synchronization_wait_ns,
                            state.snapshot.straggler_delay_ns};
    std::size_t missing = 0;
    for (std::size_t index = 0; index < metrics.size(); ++index) {
        if (!metrics_.find(metrics[index]).has_value()) {
            ++missing;
        }
        metrics_.validate_increment(metrics[index], {}, values[index]);
    }
    if (missing > maximum_metric_series_ - metrics_.size()) {
        throw std::length_error{"telemetry metric series limit exceeded"};
    }
}

void SummaryBuilder::apply_terminal_metrics(const JobState& state) {
    metrics_.increment(MetricId::JobCommunicationNs, {}, state.snapshot.communication_ns);
    metrics_.increment(MetricId::JobSynchronizationWaitNs, {},
                       state.snapshot.synchronization_wait_ns);
    metrics_.increment(MetricId::JobStragglerDelayNs, {}, state.snapshot.straggler_delay_ns);
}

bool SummaryBuilder::terminal_transition(JobTransition transition) noexcept {
    return transition == JobTransition::Succeeded || transition == JobTransition::Failed ||
           transition == JobTransition::Cancelled;
}

SummaryBuilder::JobPhase SummaryBuilder::next_phase(JobPhase current, JobTransition transition,
                                                    bool collective_active) noexcept {
    switch (transition) {
    case JobTransition::Waiting:
        return JobPhase::SchedulingWait;
    case JobTransition::Admitted:
    case JobTransition::CollectiveCompleted:
    case JobTransition::StepCompleted:
        return current == JobPhase::Compute || current == JobPhase::StragglerDelay
                   ? current
                   : JobPhase::TerminalOther;
    case JobTransition::ComputeStarted:
        return JobPhase::Compute;
    case JobTransition::StragglerStarted:
        return JobPhase::StragglerDelay;
    case JobTransition::SynchronizationStarted:
        return JobPhase::SynchronizationWait;
    case JobTransition::ComputeCompleted:
        return collective_active ? JobPhase::Communication : JobPhase::SynchronizationWait;
    case JobTransition::CollectiveStarted:
        return current == JobPhase::Compute || current == JobPhase::StragglerDelay
                   ? current
                   : JobPhase::Communication;
    case JobTransition::Succeeded:
    case JobTransition::Failed:
    case JobTransition::Cancelled:
        return JobPhase::Terminal;
    case JobTransition::Arrived:
        return JobPhase::SchedulingWait;
    }
    return JobPhase::TerminalOther;
}

void SummaryBuilder::add_interval(JobAttributionSnapshot& snapshot, JobPhase phase,
                                  std::uint64_t duration) {
    switch (phase) {
    case JobPhase::SchedulingWait:
        snapshot.scheduling_wait_ns = checked_add(snapshot.scheduling_wait_ns, duration);
        break;
    case JobPhase::Compute:
        snapshot.compute_ns = checked_add(snapshot.compute_ns, duration);
        break;
    case JobPhase::Communication:
        snapshot.communication_ns = checked_add(snapshot.communication_ns, duration);
        break;
    case JobPhase::SynchronizationWait:
        snapshot.synchronization_wait_ns = checked_add(snapshot.synchronization_wait_ns, duration);
        break;
    case JobPhase::StragglerDelay:
        snapshot.straggler_delay_ns = checked_add(snapshot.straggler_delay_ns, duration);
        break;
    case JobPhase::TerminalOther:
        snapshot.terminal_other_ns = checked_add(snapshot.terminal_other_ns, duration);
        break;
    case JobPhase::Terminal:
        break;
    }
}

} // namespace nexuslab::telemetry
