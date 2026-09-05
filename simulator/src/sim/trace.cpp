// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/trace.hpp"

#include <cstddef>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nexuslab::sim {
namespace {

constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    constexpr std::size_t byte_count = sizeof(value);
    for (std::size_t index = 0; index < byte_count; ++index) {
        constexpr std::uint64_t byte_mask = 0xFFU;
        constexpr std::size_t bits_per_byte = 8;
        const auto byte = static_cast<std::uint8_t>((value >> (index * bits_per_byte)) & byte_mask);
        hash_byte(hash, byte);
    }
}

template <typename Value, typename Encoder>
void hash_optional(std::uint64_t& hash, const std::optional<Value>& value, Encoder encoder) {
    hash_byte(hash, value.has_value() ? 1U : 0U);
    if (value.has_value()) {
        std::invoke(encoder, hash, *value);
    }
}

void hash_event_id(std::uint64_t& hash, EventId id) noexcept { hash_u64(hash, id.value()); }

void hash_time(std::uint64_t& hash, SimTimeNs time) noexcept { hash_u64(hash, time.count()); }

void hash_priority(std::uint64_t& hash, EventPriority priority) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(priority));
}

void hash_payload_kind(std::uint64_t& hash, EventPayloadKind kind) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(kind));
}

void hash_stop_reason(std::uint64_t& hash, StopReason reason) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(reason));
}

void hash_error(std::uint64_t& hash, const std::string& error) noexcept {
    hash_u64(hash, error.size());
    for (const char character : std::string_view{error}) {
        hash_byte(hash, static_cast<unsigned char>(character));
    }
}

void hash_record(std::uint64_t& hash, const TraceRecord& record) {
    hash_byte(hash, static_cast<std::uint8_t>(record.action));
    hash_time(hash, record.recorded_at);
    hash_optional(hash, record.event_id, hash_event_id);
    hash_optional(hash, record.cause, hash_event_id);
    hash_optional(hash, record.event_timestamp, hash_time);
    hash_optional(hash, record.priority, hash_priority);
    hash_optional(hash, record.payload_kind, hash_payload_kind);
    hash_optional(hash, record.stop_reason, hash_stop_reason);
    hash_optional(hash, record.error, hash_error);
}

} // namespace

EventPayloadKind payload_kind(const EventPayload& payload) {
    return std::visit(
        [](const auto& value) {
            using Payload = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Payload, NoOpEvent>) {
                return EventPayloadKind::NoOp;
            } else if constexpr (std::is_same_v<Payload, transport::ChunkArrivalEvent>) {
                return EventPayloadKind::ChunkArrival;
            } else if constexpr (std::is_same_v<Payload, transport::TransmissionCompleteEvent>) {
                return EventPayloadKind::TransmissionComplete;
            } else if constexpr (std::is_same_v<Payload, transport::LinkStateChangeEvent>) {
                return EventPayloadKind::LinkStateChange;
            } else if constexpr (std::is_same_v<Payload, transport::PortStateChangeEvent>) {
                return EventPayloadKind::PortStateChange;
            } else {
                static_assert(std::is_same_v<Payload, transport::SwitchStateChangeEvent>,
                              "event payload kind is not mapped");
                return EventPayloadKind::SwitchStateChange;
            }
        },
        payload);
}

TraceLog::TraceLog(TraceMode mode) noexcept : mode_{mode} {}

void TraceLog::append(TraceRecord record) {
    if (!enabled()) {
        return;
    }

    records_.push_back(std::move(record));
    hash_record(hash_, records_.back());
}

bool TraceLog::enabled() const noexcept { return mode_ == TraceMode::Enabled; }

std::span<const TraceRecord> TraceLog::records() const noexcept { return records_; }

std::optional<std::uint64_t> TraceLog::hash() const noexcept {
    return enabled() ? std::optional<std::uint64_t>{hash_} : std::nullopt;
}

} // namespace nexuslab::sim
