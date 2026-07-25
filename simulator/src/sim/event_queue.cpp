// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event_queue.hpp"

namespace nexuslab::sim {

void EventQueue::push(Event event) { events_.push(event); }

std::optional<Event> EventQueue::pop() {
    if (events_.empty()) {
        return std::nullopt;
    }

    Event next = events_.top();
    events_.pop();
    return next;
}

bool EventQueue::empty() const noexcept { return events_.empty(); }

std::size_t EventQueue::size() const noexcept { return events_.size(); }

bool EventQueue::LaterEvent::operator()(const Event& left, const Event& right) const noexcept {
    if (left.timestamp != right.timestamp) {
        return left.timestamp > right.timestamp;
    }
    if (left.priority != right.priority) {
        return left.priority > right.priority;
    }
    return left.id > right.id;
}

} // namespace nexuslab::sim
