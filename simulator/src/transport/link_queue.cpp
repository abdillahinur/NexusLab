// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/link_queue.hpp"

#include <algorithm>
#include <stdexcept>

namespace nexuslab::transport {
namespace {

void validate_configuration(const DirectedLinkConfiguration& configuration) {
    if (configuration.bandwidth.value() == 0) {
        throw std::invalid_argument{"directed-link bandwidth must be nonzero"};
    }

    switch (configuration.link.direction) {
    case topology::LinkDirection::AToB:
    case topology::LinkDirection::BToA:
        break;
    default:
        throw std::invalid_argument{"directed-link direction is invalid"};
    }

    if (!configuration.marking_threshold.has_value()) {
        return;
    }

    const std::uint64_t threshold = configuration.marking_threshold->value();
    if (threshold == 0) {
        throw std::invalid_argument{"marking threshold must be nonzero when enabled"};
    }
    if (threshold > configuration.waiting_buffer_capacity.value()) {
        throw std::invalid_argument{"marking threshold exceeds waiting-buffer capacity"};
    }
}

} // namespace

DirectedLinkQueue::DirectedLinkQueue(DirectedLinkConfiguration configuration)
    : configuration_{configuration} {
    validate_configuration(configuration_);
}

const DirectedLinkConfiguration& DirectedLinkQueue::configuration() const noexcept {
    return configuration_;
}

const TransferChunk* DirectedLinkQueue::active() const noexcept {
    return active_.has_value() ? &*active_ : nullptr;
}

const TransferChunk* DirectedLinkQueue::next_waiting() const noexcept {
    return waiting_.empty() ? nullptr : &waiting_.front();
}

QueueSnapshot DirectedLinkQueue::snapshot() const noexcept {
    return QueueSnapshot{
        ByteCount{waiting_bytes_}, waiting_.size(),     ByteCount{maximum_waiting_bytes_},
        maximum_waiting_chunks_,   active_.has_value(),
    };
}

AdmissionResult DirectedLinkQueue::admit(TransferChunk chunk) {
    if (chunk.bytes.value() == 0) {
        throw std::invalid_argument{"transfer chunk must contain nonzero bytes"};
    }

    if (!active_.has_value()) {
        active_ = chunk;
        return AdmissionResult{AdmissionDisposition::ServiceStarted, false};
    }

    const std::uint64_t capacity = configuration_.waiting_buffer_capacity.value();
    if (chunk.bytes.value() > capacity - waiting_bytes_) {
        return AdmissionResult{AdmissionDisposition::DroppedBufferFull, false};
    }

    const std::uint64_t admitted_waiting_bytes = waiting_bytes_ + chunk.bytes.value();
    const bool marked_here = configuration_.marking_threshold.has_value() &&
                             admitted_waiting_bytes >= configuration_.marking_threshold->value();
    chunk.marked = chunk.marked || marked_here;

    waiting_.push_back(chunk);
    waiting_bytes_ = admitted_waiting_bytes;
    maximum_waiting_bytes_ = std::max(maximum_waiting_bytes_, waiting_bytes_);
    maximum_waiting_chunks_ = std::max(maximum_waiting_chunks_, waiting_.size());
    return AdmissionResult{AdmissionDisposition::Queued, marked_here};
}

std::optional<ServiceTransition> DirectedLinkQueue::complete_service() {
    if (!active_.has_value()) {
        return std::nullopt;
    }

    const TransferChunk completed = *active_;
    active_.reset();
    std::optional<TransferChunk> next_started;

    if (!waiting_.empty()) {
        next_started = waiting_.front();
        waiting_.pop_front();
        waiting_bytes_ -= next_started->bytes.value();
        active_ = *next_started;
    }

    return ServiceTransition{completed, next_started};
}

QueueDrain DirectedLinkQueue::drain() {
    QueueDrain drained{active_, {}};
    drained.waiting.reserve(waiting_.size());
    while (!waiting_.empty()) {
        drained.waiting.push_back(waiting_.front());
        waiting_.pop_front();
    }
    active_.reset();
    waiting_bytes_ = 0;
    return drained;
}

} // namespace nexuslab::transport
