// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"

#include <stdexcept>
#include <utility>

namespace nexuslab::sim {

SimulationContext::SimulationContext(Simulation& simulation) noexcept : simulation_{&simulation} {}

EventId SimulationContext::schedule(EventSpec specification) {
    return simulation_->schedule(specification);
}

bool SimulationContext::cancel(EventId id) { return simulation_->cancel(id); }

void SimulationContext::stop(StopReason reason) { simulation_->stop(reason); }

SimTimeNs SimulationContext::now() const noexcept { return simulation_->now(); }

EventId SimulationContext::current_event_id() const { return simulation_->current_event_id(); }

std::optional<EventId> SimulationContext::cause() const { return simulation_->current_cause(); }

std::uint64_t SimulationContext::random_u64() noexcept { return simulation_->random_u64(); }

std::uint64_t SimulationContext::random_below(std::uint64_t upper_exclusive) {
    return simulation_->random_below(upper_exclusive);
}

Simulation::Simulation(std::uint64_t seed, TraceMode trace_mode) : rng_{seed}, trace_{trace_mode} {}

EventId Simulation::schedule(EventSpec specification) {
    if (lifecycle_ == Lifecycle::Finished) {
        throw std::logic_error{"cannot schedule on a finished simulation"};
    }
    if (specification.timestamp < now_) {
        throw std::invalid_argument{"cannot schedule an event in the simulated past"};
    }

    const EventId id = event_ids_.next();
    const std::optional<EventId> cause =
        current_event_.has_value() ? std::optional<EventId>{current_event_->id} : std::nullopt;
    Event event{specification.timestamp, specification.priority, id, cause, specification.payload};
    std::optional<EventTraceMetadata> metadata;
    const auto [pending_position, inserted] = pending_ids_.insert(id.value());
    if (!inserted) {
        throw std::logic_error{"duplicate pending event ID"};
    }

    try {
        if (trace_.enabled()) {
            metadata = event_trace_metadata(event);
            const auto [unused, metadata_inserted] = trace_metadata_.emplace(id.value(), *metadata);
            if (!metadata_inserted) {
                throw std::logic_error{"duplicate event trace metadata"};
            }
        }
        event_queue_.push(event);
    } catch (...) {
        pending_ids_.erase(pending_position);
        trace_metadata_.erase(id.value());
        throw;
    }
    if (metadata.has_value()) {
        record_event(TraceAction::Scheduled, now_, *metadata);
    }
    return id;
}

bool Simulation::cancel(EventId id) {
    if (lifecycle_ == Lifecycle::Finished) {
        return false;
    }
    if (!pending_ids_.contains(id.value())) {
        return false;
    }

    std::optional<EventTraceMetadata> metadata;
    if (trace_.enabled()) {
        const auto position = trace_metadata_.find(id.value());
        if (position == trace_metadata_.end()) {
            throw std::logic_error{"missing event trace metadata"};
        }
        metadata = position->second;
    }

    const auto [unused, inserted] = cancelled_ids_.insert(id.value());
    if (!inserted) {
        return false;
    }
    ++cancelled_events_;
    if (metadata.has_value()) {
        record_event(TraceAction::Cancelled, now_, *metadata);
    }
    return true;
}

void Simulation::stop(StopReason reason) {
    if (lifecycle_ != Lifecycle::Running) {
        throw std::logic_error{"simulation can only be stopped while running"};
    }
    if (!current_event_.has_value()) {
        throw std::logic_error{"simulation can only be stopped during event dispatch"};
    }
    if (!stop_reason_.has_value()) {
        if (trace_.enabled()) {
            record_event(TraceAction::StopRequested, now_, event_trace_metadata(*current_event_),
                         reason);
        }
        stop_reason_ = reason;
    }
}

SimTimeNs Simulation::now() const noexcept { return now_; }

std::span<const TraceRecord> Simulation::trace_records() const noexcept { return trace_.records(); }

Simulation::EventTraceMetadata Simulation::event_trace_metadata(const Event& event) {
    return EventTraceMetadata{event.timestamp, event.priority, event.id, event.cause,
                              payload_kind(event.payload)};
}

void Simulation::record_event(TraceAction action, SimTimeNs recorded_at,
                              const EventTraceMetadata& metadata,
                              std::optional<StopReason> stop_reason) {
    trace_.append(TraceRecord{action, recorded_at, metadata.id, metadata.cause, metadata.timestamp,
                              metadata.priority, metadata.payload_kind, stop_reason, std::nullopt});
}

void Simulation::record_terminal(TraceAction action, const std::optional<std::string>& error) {
    if (!trace_.enabled()) {
        return;
    }

    std::optional<EventTraceMetadata> metadata;
    if (current_event_.has_value()) {
        metadata = event_trace_metadata(*current_event_);
    }

    trace_.append(TraceRecord{
        action, now_, metadata.has_value() ? std::optional<EventId>{metadata->id} : std::nullopt,
        metadata.has_value() ? metadata->cause : std::nullopt,
        metadata.has_value() ? std::optional<SimTimeNs>{metadata->timestamp} : std::nullopt,
        metadata.has_value() ? std::optional<EventPriority>{metadata->priority} : std::nullopt,
        metadata.has_value() ? std::optional<EventPayloadKind>{metadata->payload_kind}
                             : std::nullopt,
        std::nullopt, error});
}

void Simulation::begin_run() {
    if (lifecycle_ != Lifecycle::Created) {
        throw std::logic_error{"simulation can only be run once"};
    }
    lifecycle_ = Lifecycle::Running;
}

std::optional<Event> Simulation::next_dispatchable_event() {
    while (true) {
        auto next = event_queue_.pop();
        if (!next.has_value()) {
            return std::nullopt;
        }

        pending_ids_.erase(next->id.value());
        trace_metadata_.erase(next->id.value());
        if (cancelled_ids_.erase(next->id.value()) != 0) {
            continue;
        }
        return next;
    }
}

SimulationResult Simulation::finish(SimulationStatus status, std::optional<std::string> error) {
    if (status == SimulationStatus::Completed) {
        record_terminal(TraceAction::Completed, std::nullopt);
    } else if (status == SimulationStatus::Failed) {
        record_terminal(TraceAction::Failed, error);
    }

    lifecycle_ = Lifecycle::Finished;
    current_event_.reset();
    const std::size_t pending_events = pending_ids_.size() - cancelled_ids_.size();
    return SimulationResult{
        status,         stop_reason_,      now_,          dispatched_events_, cancelled_events_,
        pending_events, rng_.draw_count(), trace_.hash(), std::move(error)};
}

SimulationResult Simulation::fail(std::string error) {
    return finish(SimulationStatus::Failed, std::move(error));
}

EventId Simulation::current_event_id() const {
    if (!current_event_.has_value()) {
        throw std::logic_error{"no event is currently being dispatched"};
    }
    return current_event_->id;
}

std::optional<EventId> Simulation::current_cause() const {
    if (!current_event_.has_value()) {
        throw std::logic_error{"no event is currently being dispatched"};
    }
    return current_event_->cause;
}

std::uint64_t Simulation::random_u64() noexcept { return rng_.next_u64(); }

std::uint64_t Simulation::random_below(std::uint64_t upper_exclusive) {
    return rng_.uniform_below(upper_exclusive);
}

} // namespace nexuslab::sim
