// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/link_service.hpp"

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/transport/timing.hpp"

#include <stdexcept>

namespace nexuslab::transport {

DirectedLinkService::DirectedLinkService(DirectedLinkConfiguration configuration)
    : queue_{configuration} {}

const DirectedLinkQueue& DirectedLinkService::queue() const noexcept { return queue_; }

std::optional<sim::EventId> DirectedLinkService::scheduled_completion() const noexcept {
    return scheduled_completion_;
}

AdmissionResult DirectedLinkService::admit(TransferChunk chunk, sim::SimulationContext& context) {
    if (chunk.bytes.value() == 0) {
        throw std::invalid_argument{"transfer chunk must contain nonzero bytes"};
    }

    if (queue_.active() != nullptr) {
        return queue_.admit(chunk);
    }

    const auto delay = serialization_delay(chunk.bytes, queue_.configuration().bandwidth);
    if (!delay.has_value()) {
        throw std::overflow_error{"serialization delay exceeds simulated-time range"};
    }
    const auto completion_time = sim::checked_add(context.now(), *delay);
    if (!completion_time.has_value()) {
        throw std::overflow_error{"serialization completion exceeds simulated-time range"};
    }
    const sim::SimTimeNs validated_completion_time = completion_time.value();

    const AdmissionResult result = queue_.admit(chunk);
    if (result.disposition != AdmissionDisposition::ServiceStarted) {
        throw std::logic_error{"idle directed-link queue did not start admitted chunk"};
    }

    scheduled_completion_ = context.schedule(sim::EventSpec{
        validated_completion_time,
        sim::EventPriority::Control,
        sim::EventPayload{TransmissionCompleteEvent{
            queue_.configuration().link.link,
            chunk.id,
            chunk.hop_index,
            queue_.configuration().link.direction,
        }},
    });
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

    const auto transition = queue_.complete_service();
    if (!transition.has_value()) {
        throw std::logic_error{"active service disappeared before completion"};
    }

    scheduled_completion_.reset();
    if (transition->next_started.has_value()) {
        scheduled_completion_ = schedule_completion(*transition->next_started, context);
    }
    return *transition;
}

QueueDrain DirectedLinkService::reconcile_down(sim::SimulationContext& context) {
    if (scheduled_completion_.has_value()) {
        if (!context.cancel(*scheduled_completion_)) {
            throw std::logic_error{"active transmission completion could not be cancelled"};
        }
        scheduled_completion_.reset();
    }
    return queue_.drain();
}

sim::EventId DirectedLinkService::schedule_completion(const TransferChunk& chunk,
                                                      sim::SimulationContext& context) {
    const auto delay = serialization_delay(chunk.bytes, queue_.configuration().bandwidth);
    if (!delay.has_value()) {
        throw std::overflow_error{"serialization delay exceeds simulated-time range"};
    }
    const auto completion_time = sim::checked_add(context.now(), *delay);
    if (!completion_time.has_value()) {
        throw std::overflow_error{"serialization completion exceeds simulated-time range"};
    }

    return context.schedule(sim::EventSpec{
        *completion_time,
        sim::EventPriority::Control,
        sim::EventPayload{TransmissionCompleteEvent{
            queue_.configuration().link.link,
            chunk.id,
            chunk.hop_index,
            queue_.configuration().link.direction,
        }},
    });
}

} // namespace nexuslab::transport
