// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/workload/engine.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace nexuslab::workload {
JobSnapshot WorkloadEngine::inspect(const Record& record, sim::SimTimeNs now) {
    const auto end = record.finished.value_or(now);
    if (end < record.spec.arrival) {
        return {record.id,
                record.state,
                record.step,
                record.spec.arrival,
                record.finished,
                0,
                0,
                0,
                record.reason,
                record.allocated_at,
                0};
    }
    const auto elapsed = end.count() - record.spec.arrival.count();
    auto compute = record.compute_gpu_ns;
    if (record.assigned && !terminal(record.state)) {
        if (now < record.step_started) {
            throw std::invalid_argument{"snapshot precedes current step"};
        }
        for (const auto duration : record.spec.compute) {
            compute = checked_sum(
                compute, std::min(duration.count(), now.count() - record.step_started.count()));
        }
    }
    const auto wait_end = record.allocated_at.value_or(end);
    const auto waiting = wait_end.count() - record.spec.arrival.count();
    const auto allocated = record.allocated_at.has_value()
                               ? checked_product(end.count() - record.allocated_at->count(),
                                                 record.spec.workers.size())
                               : 0;
    if (compute > allocated) {
        throw std::logic_error{"GPU compute exceeds allocated time"};
    }
    return {record.id, record.state,        record.step, record.spec.arrival, record.finished,
            compute,   allocated - compute, elapsed,     record.reason,       record.allocated_at,
            waiting};
}
void WorkloadEngine::finish(Record& record, JobState state, std::string reason,
                            sim::SimulationContext& context) {
    if (terminal(record.state)) {
        return;
    }
    if (state != JobState::Succeeded && record.assigned) {
        record.compute_gpu_ns = inspect(record, context.now()).compute_gpu_ns;
    }
    for (const auto& event : record.events) {
        if (event.has_value()) {
            static_cast<void>(context.cancel(*event));
        }
    }
    if (record.arrival_event.has_value()) {
        static_cast<void>(context.cancel(*record.arrival_event));
        record.arrival_event.reset();
    }
    if (record.active_collective.has_value()) {
        executor_->cancel(*record.active_collective);
    }
    if (record.assigned) {
        for (const auto worker : record.spec.workers) {
            assignments_.erase(worker);
        }
    }
    if (inventory_) {
        inventory_->release(record.id);
        admission_pending_ = true;
    }
    record.state = state;
    record.reason = std::move(reason);
    record.finished = context.now();
    trace(record, context.now(), "job_terminal");
    completed_.push_back(inspect(record, context.now()));
}
std::optional<JobSnapshot> WorkloadEngine::snapshot(JobId id, sim::SimTimeNs now) const {
    const auto found = jobs_.find(id);
    return found == jobs_.end() ? std::nullopt : std::optional{inspect(found->second, now)};
}
std::vector<JobSnapshot> WorkloadEngine::take_completed() { return std::exchange(completed_, {}); }
std::span<const JobTimeline> WorkloadEngine::timeline() const noexcept { return timeline_; }
} // namespace nexuslab::workload
