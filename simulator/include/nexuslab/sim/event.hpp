// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/sim/time.hpp"
#include "nexuslab/transport/events.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <variant>

namespace nexuslab::sim {

enum class EventPriority : std::uint8_t {
    Critical = 0,
    Control = 64,
    Normal = 128,
    Background = 192,
};

struct NoOpEvent final {
    std::uint64_t token{0};

    auto operator<=>(const NoOpEvent&) const = default;
};

using EventPayload =
    std::variant<NoOpEvent, transport::ChunkArrivalEvent, transport::TransmissionCompleteEvent>;

struct EventSpec final {
    SimTimeNs timestamp;
    EventPriority priority{EventPriority::Normal};
    EventPayload payload;

    bool operator==(const EventSpec&) const = default;
};

struct Event final {
    SimTimeNs timestamp;
    EventPriority priority;
    EventId id;
    std::optional<EventId> cause;
    EventPayload payload;

    bool operator==(const Event&) const = default;
};

} // namespace nexuslab::sim
