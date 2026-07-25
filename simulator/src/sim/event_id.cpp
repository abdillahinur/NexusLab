// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event_id.hpp"

#include <limits>
#include <stdexcept>

namespace nexuslab::sim {

EventIdGenerator::EventIdGenerator(std::uint64_t next_value) noexcept : next_value_{next_value} {}

EventId EventIdGenerator::next() {
    if (exhausted_) {
        throw std::overflow_error{"event ID sequence exhausted"};
    }

    const EventId result{next_value_};
    if (next_value_ == std::numeric_limits<std::uint64_t>::max()) {
        exhausted_ = true;
    } else {
        ++next_value_;
    }
    return result;
}

} // namespace nexuslab::sim
