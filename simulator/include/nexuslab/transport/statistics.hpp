// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/time.hpp"
#include "nexuslab/transport/types.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nexuslab::transport {

struct TrafficCount final {
    std::uint64_t bytes{0};
    std::uint64_t chunks{0};
    bool operator==(const TrafficCount&) const = default;
};

[[nodiscard]] inline TrafficCount add_traffic(TrafficCount left, TrafficCount right) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (right.bytes > maximum - left.bytes || right.chunks > maximum - left.chunks) {
        throw std::overflow_error{"transport counter overflow"};
    }
    return TrafficCount{left.bytes + right.bytes, left.chunks + right.chunks};
}

struct LinkStatistics final {
    TrafficCount enqueued;
    TrafficCount started;
    TrafficCount completed;
    TrafficCount marked;
    TrafficCount dropped_buffer_full;
    TrafficCount dropped_link_down;
    sim::SimDurationNs busy_time;
    bool operator==(const LinkStatistics&) const = default;
};

} // namespace nexuslab::transport
