// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/workload/engine.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace nexuslab::workload {
sim::EventId WorkloadEngine::schedule_gpu_state(topology::GpuId gpu, bool healthy,
                                                sim::SimTimeNs when,
                                                sim::Simulation& simulation) const {
    if (!inventory_ || graph_->find(gpu) == nullptr ||
        gpu.value() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument{"GPU state requires scheduler and known GPU"};
    }
    return simulation.schedule(
        {when, sim::EventPriority::Critical,
         WorkloadEvent{JobId{0}, healthy ? WorkloadEventKind::GpuUp : WorkloadEventKind::GpuDown,
                       static_cast<std::uint32_t>(gpu.value())}});
}
void WorkloadEngine::gpu_state(const WorkloadEvent& event, sim::SimulationContext& context) {
    if (!inventory_) {
        throw std::logic_error{"GPU state without scheduler"};
    }
    const auto owner = inventory_->set_health(topology::GpuId{event.worker},
                                              event.kind == WorkloadEventKind::GpuUp);
    if (owner.has_value() && event.kind == WorkloadEventKind::GpuDown) {
        finish(jobs_.at(*owner), JobState::Failed, "allocated GPU failed", context);
    }
    admission_pending_ = true;
}
void WorkloadEngine::dispatch_waiting(sim::SimulationContext& context) {
    if (!admission_pending_ || !inventory_) {
        return;
    }
    admission_pending_ = false;
    std::vector<Record*> waiting;
    for (auto& [id, record] : jobs_) {
        if (record.state == JobState::Waiting) {
            waiting.push_back(&record);
        }
    }
    std::sort(waiting.begin(), waiting.end(), [](const auto* a, const auto* b) {
        if (a->spec.priority != b->spec.priority) {
            return a->spec.priority > b->spec.priority;
        }
        if (a->spec.arrival != b->spec.arrival) {
            return a->spec.arrival < b->spec.arrival;
        }
        return a->id < b->id;
    });
    for (auto* record : waiting) {
        admit(*record, context);
    }
    // Rejections release no allocation; they do not require another identical pass.
    admission_pending_ = false;
}
void WorkloadEngine::admit(Record& record, sim::SimulationContext& context) {
    if (!scheduling_.has_value()) {
        throw std::logic_error{"admission requires scheduler"};
    }
    const auto& config = *scheduling_;
    if (placements_.size() == config.decision_entries) {
        throw std::length_error{"scheduler decision limit exceeded"};
    }
    const auto before = scheduling::fragmentation(inventory_->view());
    const auto requested = static_cast<std::uint32_t>(record.spec.compute.size());
    auto placement = policy_->place(
        {record.id, requested, record.spec.priority, record.spec.workers}, inventory_->view());
    if (placement.reason.empty() || placement.reason.size() > 256 ||
        (placement.outcome != scheduling::PlacementOutcome::Placed &&
         placement.outcome != scheduling::PlacementOutcome::Waiting &&
         placement.outcome != scheduling::PlacementOutcome::Rejected) ||
        (placement.outcome != scheduling::PlacementOutcome::Placed && !placement.workers.empty())) {
        throw std::logic_error{"invalid scheduling policy result"};
    }
    scheduling::Locality local;
    if (placement.outcome == scheduling::PlacementOutcome::Placed) {
        if (placement.workers.size() != requested ||
            (!record.spec.workers.empty() && placement.workers != record.spec.workers)) {
            throw std::logic_error{"policy violated allocation dimensions or pinned order"};
        }
        if (placement.workers.size() > config.allocation_entries - allocation_entries_) {
            throw std::length_error{"scheduler allocation record limit exceeded"};
        }
        inventory_->allocate(record.id, placement.workers);
        allocation_entries_ += placement.workers.size();
        local = scheduling::locality(inventory_->view(), placement.workers);
    }
    placements_.push_back({record.id, context.now(), config.policy, 1, record.spec.priority,
                           requested, placement.outcome, placement.workers, placement.reason, local,
                           before, scheduling::fragmentation(inventory_->view())});
    record.reason = placement.reason;
    if (placement.outcome == scheduling::PlacementOutcome::Rejected) {
        finish(record, JobState::Failed, placement.reason, context);
    } else if (placement.outcome == scheduling::PlacementOutcome::Placed) {
        record.spec.workers = std::move(placement.workers);
        for (const auto gpu : record.spec.workers) {
            assignments_.emplace(gpu, record.id);
        }
        record.assigned = true;
        record.allocated_at = context.now();
        trace(record, context.now(), "job_allocated");
        start_step(record, context);
    }
}
std::span<const scheduling::PlacementDecision> WorkloadEngine::placements() const noexcept {
    return placements_;
}
} // namespace nexuslab::workload
