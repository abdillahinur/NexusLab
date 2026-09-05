// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/link_service.hpp"
#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/transport/timing.hpp"
#include <stdexcept>

namespace nexuslab::transport {
namespace {
[[nodiscard]] sim::SimTimeNs completion_time(const TransferChunk& chunk,
                                             const DirectedLinkConfiguration& config,
                                             sim::SimTimeNs now) {
    const auto delay = serialization_delay(chunk.bytes, config.bandwidth);
    if (!delay.has_value()) {
        throw std::overflow_error{"serialization delay exceeds simulated-time range"};
    }
    const auto time = sim::checked_add(now, *delay);
    if (!time.has_value()) {
        throw std::overflow_error{"serialization completion exceeds simulated-time range"};
    }
    if (!sim::checked_add(*time, config.propagation_delay).has_value()) {
        throw std::overflow_error{"propagation arrival exceeds simulated-time range"};
    }
    return *time;
}
} // namespace

DirectedLinkService::DirectedLinkService(DirectedLinkConfiguration configuration)
    : queue_{configuration} {}

const DirectedLinkQueue& DirectedLinkService::queue() const noexcept { return queue_; }

std::optional<sim::EventId> DirectedLinkService::scheduled_completion() const noexcept {
    return scheduled_completion_;
}

LinkStatistics DirectedLinkService::statistics(sim::SimTimeNs now) const {
    LinkStatistics result = statistics_;
    if (busy_since_.has_value()) {
        const auto elapsed = sim::checked_difference(now, *busy_since_);
        if (!elapsed.has_value()) {
            throw std::invalid_argument{"statistics time precedes active service"};
        }
        const auto total = sim::checked_add(sim::SimTimeNs{result.busy_time.count()}, *elapsed);
        if (!total.has_value()) {
            throw std::overflow_error{"serializer busy time overflow"};
        }
        result.busy_time = sim::SimDurationNs{total->count()};
    }
    return result;
}

AdmissionResult DirectedLinkService::admit(TransferChunk chunk, sim::SimulationContext& context) {
    if (chunk.bytes.value() == 0) {
        throw std::invalid_argument{"transfer chunk must contain nonzero bytes"};
    }
    const auto now = context.now();
    static_cast<void>(completion_time(chunk, queue_.configuration(), now));
    const TrafficCount count{chunk.bytes.value(), 1};
    LinkStatistics next = statistics_;
    const auto threshold = queue_.configuration().marking_threshold;
    const bool idle = queue_.active() == nullptr;
    const QueueSnapshot snapshot = queue_.snapshot();
    const bool dropped =
        !idle && chunk.bytes.value() > queue_.configuration().waiting_buffer_capacity.value() -
                                           snapshot.waiting_bytes.value();
    if (dropped) {
        next.dropped_buffer_full = add_traffic(next.dropped_buffer_full, count);
    } else {
        next.enqueued = add_traffic(next.enqueued, count);
        if (idle) {
            next.started = add_traffic(next.started, count);
        } else if (threshold.has_value() &&
                   snapshot.waiting_bytes.value() + chunk.bytes.value() >= threshold->value()) {
            next.marked = add_traffic(next.marked, count);
        }
    }
    std::optional<sim::EventId> completion;
    if (idle) {
        completion = schedule_completion(chunk, context);
    }
    const AdmissionResult result = queue_.admit(chunk);
    statistics_ = next;
    if (idle) {
        scheduled_completion_ = completion;
        busy_since_ = now;
    }
    return result;
}

ServiceTransition DirectedLinkService::handle_completion(const TransmissionCompleteEvent& event,
                                                         sim::SimulationContext& context) {
    if (!scheduled_completion_.has_value() ||
        *scheduled_completion_ != context.current_event_id()) {
        throw std::logic_error{"unexpected transmission-completion event"};
    }
    const TransferChunk* active = queue_.active();
    if (active == nullptr || event.link != queue_.configuration().link.link ||
        event.direction != queue_.configuration().link.direction || event.chunk != active->id ||
        event.hop_index != active->hop_index) {
        throw std::logic_error{"transmission-completion event does not match active service"};
    }
    LinkStatistics next = statistics(context.now());
    next.completed = add_traffic(next.completed, TrafficCount{active->bytes.value(), 1});
    std::optional<sim::EventId> completion;
    const TransferChunk* waiting = queue_.next_waiting();
    if (waiting != nullptr) {
        next.started = add_traffic(next.started, TrafficCount{waiting->bytes.value(), 1});
        completion = schedule_completion(*waiting, context);
    }
    const auto transition = queue_.complete_service();
    if (!transition.has_value()) {
        throw std::logic_error{"active service disappeared before completion"};
    }
    statistics_ = next;
    scheduled_completion_ = completion;
    busy_since_ = completion.has_value() ? std::optional{context.now()} : std::nullopt;
    return *transition;
}

void DirectedLinkService::record_link_down(const TransferChunk& chunk) {
    statistics_.dropped_link_down =
        add_traffic(statistics_.dropped_link_down, TrafficCount{chunk.bytes.value(), 1});
}

QueueDrain DirectedLinkService::reconcile_down(sim::SimulationContext& context) {
    LinkStatistics next = statistics(context.now());
    const auto snapshot = queue_.snapshot();
    TrafficCount dropped{snapshot.waiting_bytes.value(), snapshot.waiting_chunks};
    if (queue_.active() != nullptr) {
        dropped = add_traffic(dropped, TrafficCount{queue_.active()->bytes.value(), 1});
    }
    next.dropped_link_down = add_traffic(next.dropped_link_down, dropped);
    if (scheduled_completion_.has_value()) {
        if (!context.cancel(*scheduled_completion_)) {
            throw std::logic_error{"active transmission completion could not be cancelled"};
        }
    }
    QueueDrain drained = queue_.drain();
    scheduled_completion_.reset();
    busy_since_.reset();
    statistics_ = next;
    return drained;
}

sim::EventId DirectedLinkService::schedule_completion(const TransferChunk& chunk,
                                                      sim::SimulationContext& context) {
    const auto time = completion_time(chunk, queue_.configuration(), context.now());
    return context.schedule(sim::EventSpec{
        time, sim::EventPriority::Control,
        sim::EventPayload{TransmissionCompleteEvent{queue_.configuration().link.link, chunk.id,
                                                    chunk.hop_index,
                                                    queue_.configuration().link.direction}}});
}
} // namespace nexuslab::transport
