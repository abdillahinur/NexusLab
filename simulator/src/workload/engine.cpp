// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/workload/engine.hpp"
#include "nexuslab/sim/simulation.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>
namespace nexuslab::workload {
WorkloadEngine::WorkloadEngine(const topology::TopologyGraph& graph,
                               collective::CollectiveExecutor& executor, WorkloadLimits limits)
    : graph_{&graph}, executor_{&executor}, limits_{limits} {
    if (limits.worker_entries == 0 || limits.compute_event_entries == 0 || limits.jobs == 0 ||
        limits.workers_per_job == 0 || limits.workers_per_job > 8192 || limits.steps_per_job == 0 ||
        limits.buckets_per_step == 0 || limits.compute_events_per_job == 0 ||
        limits.timeline_entries == 0) {
        throw std::invalid_argument{"invalid workload limits"};
    }
}
std::uint32_t WorkloadEngine::validate(const JobSpec& spec) const {
    if (spec.collective != CollectiveKind::AllReduce ||
        spec.algorithm != CollectiveAlgorithm::Ring || spec.name.empty() ||
        spec.name.size() > 256 || spec.workers.empty() ||
        spec.workers.size() > limits_.workers_per_job ||
        spec.compute.size() != spec.workers.size() || spec.steps == 0 ||
        spec.steps > limits_.steps_per_job || spec.gradient_bytes.value() == 0 ||
        spec.bucket_bytes.value() == 0 || spec.chunk_bytes.value() == 0) {
        throw std::invalid_argument{"invalid workload job specification"};
    }
    const auto buckets =
        spec.gradient_bytes.value() / spec.bucket_bytes.value() +
        static_cast<std::uint64_t>(spec.gradient_bytes.value() % spec.bucket_bytes.value() != 0);
    if (buckets > limits_.buckets_per_step ||
        checked_product(spec.workers.size(), spec.overlap ? buckets : 1) >
            limits_.compute_events_per_job) {
        throw std::length_error{"workload bucket or compute-event limit exceeded"};
    }
    std::set<topology::GpuId> unique;
    std::uint64_t compute_total{0};
    for (std::size_t index = 0; index < spec.workers.size(); ++index) {
        if (graph_->find(spec.workers[index]) == nullptr ||
            !unique.insert(spec.workers[index]).second || spec.compute[index].count() == 0) {
            throw std::invalid_argument{"invalid worker assignment or compute duration"};
        }
        compute_total = checked_sum(compute_total, spec.compute[index].count());
        static_cast<void>(checked_sum(spec.arrival.count(),
                                      checked_product(spec.steps, spec.compute[index].count())));
    }
    static_cast<void>(checked_product(compute_total, spec.steps));
    return static_cast<std::uint32_t>(buckets);
}
JobId WorkloadEngine::schedule(JobSpec specification, sim::Simulation& simulation) {
    const auto buckets = validate(specification);
    if (jobs_.size() == limits_.jobs) {
        throw std::length_error{"workload job limit exceeded"};
    }
    const auto slots = specification.workers.size() * (specification.overlap ? buckets : 1);
    if (specification.workers.size() > limits_.worker_entries - worker_entries_ ||
        slots > limits_.compute_event_entries - event_entries_) {
        throw std::length_error{"workload global retained-state limit exceeded"};
    }
    const JobId id{jobs_.size()};
    const auto event = simulation.schedule({specification.arrival, sim::EventPriority::Normal,
                                            WorkloadEvent{id, WorkloadEventKind::Arrival}});
    Record record{id, std::move(specification)};
    record.buckets = buckets;
    record.arrival_event = event;
    worker_entries_ += record.spec.workers.size();
    event_entries_ += slots;
    jobs_.emplace(id, std::move(record));
    return id;
}
sim::EventId WorkloadEngine::schedule_control(JobId job, WorkloadEventKind kind,
                                              sim::SimTimeNs when, sim::Simulation& simulation,
                                              std::uint32_t worker) const {
    const auto& record = jobs_.at(job);
    if ((kind != WorkloadEventKind::Cancel && kind != WorkloadEventKind::WorkerFailure) ||
        worker >= record.spec.workers.size()) {
        throw std::invalid_argument{"invalid workload control event"};
    }
    return simulation.schedule(
        {when, sim::EventPriority::Critical, WorkloadEvent{job, kind, worker}});
}
void WorkloadEngine::handle(const WorkloadEvent& event, sim::SimulationContext& context) {
    auto& record = jobs_.at(event.job);
    if (terminal(record.state)) {
        return;
    }
    switch (event.kind) {
    case WorkloadEventKind::Arrival:
        if (!record.arrival_event.has_value() ||
            *record.arrival_event != context.current_event_id()) {
            throw std::logic_error{"unexpected job arrival"};
        }
        record.arrival_event.reset();
        arrive(record, context);
        break;
    case WorkloadEventKind::ComputeReady:
        compute_ready(record, event, context);
        break;
    case WorkloadEventKind::Cancel:
        finish(record, JobState::Cancelled, "job cancelled", context);
        break;
    case WorkloadEventKind::WorkerFailure:
        if (event.worker >= record.spec.workers.size()) {
            throw std::invalid_argument{"unknown failed worker"};
        }
        finish(record, JobState::Failed, "worker failed", context);
        break;
    default:
        throw std::invalid_argument{"unknown workload event"};
    }
}
void WorkloadEngine::arrive(Record& record, sim::SimulationContext& context) {
    for (const auto worker : record.spec.workers) {
        if (assignments_.contains(worker)) {
            finish(record, JobState::Failed, "GPU assignment conflict", context);
            return;
        }
    }
    for (const auto worker : record.spec.workers) {
        assignments_.emplace(worker, record.id);
    }
    record.assigned = true;
    start_step(record, context);
}
void WorkloadEngine::trace(const Record& record, sim::SimTimeNs now, std::string action) {
    if (timeline_.size() == limits_.timeline_entries) {
        throw std::length_error{"workload timeline limit exceeded"};
    }
    timeline_.push_back({record.id, now, record.state, record.step, record.completed_buckets,
                         std::move(action), record.active_collective});
}
void WorkloadEngine::start_step(Record& record, sim::SimulationContext& context) {
    record.state = JobState::Computing;
    record.step_started = context.now();
    record.completed_buckets = 0;
    record.compute_complete = 0;
    record.ready.assign(record.buckets, 0);
    const auto events_per_worker = record.spec.overlap ? record.buckets : 1;
    record.events.assign(record.spec.workers.size() * events_per_worker, std::nullopt);
    for (std::uint32_t worker = 0; worker < record.spec.workers.size(); ++worker) {
        const auto duration = record.spec.compute[worker].count();
        for (std::uint32_t index = 0; index < events_per_worker; ++index) {
            const auto fraction = index + 1;
            const auto offset = checked_sum(
                checked_product(duration / events_per_worker, fraction),
                (checked_product(duration % events_per_worker, fraction) + events_per_worker - 1) /
                    events_per_worker);
            const auto time = sim::checked_add(context.now(), sim::SimDurationNs{offset});
            if (!time.has_value()) {
                throw std::overflow_error{"compute event time overflow"};
            }
            const auto bucket = record.spec.overlap ? index : record.buckets - 1;
            record.events[static_cast<std::size_t>(worker) * events_per_worker + index] =
                context.schedule({*time, sim::EventPriority::Normal,
                                  WorkloadEvent{record.id, WorkloadEventKind::ComputeReady, worker,
                                                record.step, bucket}});
        }
    }
    trace(record, context.now(), "step_compute_started");
}
void WorkloadEngine::compute_ready(Record& record, const WorkloadEvent& event,
                                   sim::SimulationContext& context) {
    if (record.state == JobState::Scheduled || event.step != record.step ||
        event.worker >= record.spec.workers.size() || event.bucket >= record.buckets) {
        throw std::logic_error{"stale workload compute event"};
    }
    const auto per_worker = record.spec.overlap ? record.buckets : 1;
    const auto index = static_cast<std::size_t>(event.worker) * per_worker +
                       (record.spec.overlap ? event.bucket : 0);
    auto& pending = record.events[index];
    if (!pending.has_value() || *pending != context.current_event_id()) {
        throw std::logic_error{"unexpected compute completion"};
    }
    pending.reset();
    if (record.spec.overlap) {
        ++record.ready[event.bucket];
    } else {
        for (auto& ready : record.ready) {
            ++ready;
        }
    }
    if (event.bucket == record.buckets - 1) {
        ++record.compute_complete;
    }
    if (record.compute_complete == record.spec.workers.size()) {
        if (record.active_collective.has_value()) {
            record.state = JobState::Communicating;
        }
        trace(record, context.now(), "step_compute_finished");
    }
    start_collective(record, context);
}
void WorkloadEngine::start_collective(Record& record, sim::SimulationContext& context) {
    if (record.active_collective.has_value() ||
        record.ready[record.completed_buckets] != record.spec.workers.size()) {
        return;
    }
    const auto consumed =
        checked_product(record.completed_buckets, record.spec.bucket_bytes.value());
    const auto bytes =
        std::min(record.spec.bucket_bytes.value(), record.spec.gradient_bytes.value() - consumed);
    const auto id = executor_->submit(
        {record.spec.workers, transport::ByteCount{bytes}, record.spec.chunk_bytes}, context);
    record.active_collective = id;
    collectives_.emplace(id, record.id);
    record.state = record.compute_complete == record.spec.workers.size() ? JobState::Communicating
                                                                         : JobState::Overlapping;
    trace(record, context.now(), "bucket_collective_started");
}
void WorkloadEngine::handle(const collective::CollectiveResult& completion,
                            sim::SimulationContext& context) {
    const auto found = collectives_.find(completion.id);
    if (found == collectives_.end()) {
        throw std::logic_error{"unowned collective result"};
    }
    auto& record = jobs_.at(found->second);
    collectives_.erase(found);
    if (terminal(record.state)) {
        record.active_collective.reset();
        return;
    }
    if (completion.outcome != collective::Phase::Succeeded) {
        record.active_collective.reset();
        finish(record, JobState::Failed, completion.reason, context);
        return;
    }
    trace(record, context.now(), "bucket_collective_completed");
    record.active_collective.reset();
    ++record.completed_buckets;
    if (record.completed_buckets == record.buckets) {
        for (const auto duration : record.spec.compute) {
            record.compute_gpu_ns = checked_sum(record.compute_gpu_ns, duration.count());
        }
        ++record.step;
        if (record.step == record.spec.steps) {
            finish(record, JobState::Succeeded, "all training steps completed", context);
        } else {
            start_step(record, context);
        }
    } else {
        record.state = record.compute_complete == record.spec.workers.size()
                           ? JobState::Communicating
                           : JobState::Computing;
        start_collective(record, context);
    }
}
} // namespace nexuslab::workload
