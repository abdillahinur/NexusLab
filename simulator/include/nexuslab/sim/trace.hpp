// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nexuslab::sim {

enum class TraceMode : std::uint8_t {
    Disabled = 0,
    Enabled = 1,
};

enum class TraceAction : std::uint8_t {
    Scheduled = 1,
    Dispatched = 2,
    Cancelled = 3,
    StopRequested = 4,
    Completed = 5,
    Failed = 6,
};

enum class EventPayloadKind : std::uint8_t {
    NoOp = 1,
    ChunkArrival = 2,
    TransmissionComplete = 3,
};

enum class StopReason : std::uint8_t {
    Requested = 1,
};

struct TraceRecord final {
    TraceAction action;
    SimTimeNs recorded_at;
    std::optional<EventId> event_id;
    std::optional<EventId> cause;
    std::optional<SimTimeNs> event_timestamp;
    std::optional<EventPriority> priority;
    std::optional<EventPayloadKind> payload_kind;
    std::optional<StopReason> stop_reason;
    std::optional<std::string> error;

    bool operator==(const TraceRecord&) const = default;
};

[[nodiscard]] EventPayloadKind payload_kind(const EventPayload& payload);

class TraceLog final {
  public:
    explicit TraceLog(TraceMode mode = TraceMode::Enabled) noexcept;

    void append(TraceRecord record);

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::span<const TraceRecord> records() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> hash() const noexcept;

  private:
    static constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;

    TraceMode mode_;
    std::vector<TraceRecord> records_;
    std::uint64_t hash_{fnv_offset_basis};
};

} // namespace nexuslab::sim
