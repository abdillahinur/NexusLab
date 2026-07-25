// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>

namespace nexuslab::sim {

class EventId final {
  public:
    explicit constexpr EventId(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    auto operator<=>(const EventId&) const = default;

  private:
    std::uint64_t value_;
};

class EventIdGenerator final {
  public:
    explicit EventIdGenerator(std::uint64_t next_value = 0) noexcept;
    EventIdGenerator(const EventIdGenerator&) = delete;
    EventIdGenerator& operator=(const EventIdGenerator&) = delete;
    EventIdGenerator(EventIdGenerator&&) = delete;
    EventIdGenerator& operator=(EventIdGenerator&&) = delete;

    [[nodiscard]] EventId next();

  private:
    std::uint64_t next_value_;
    bool exhausted_{false};
};

} // namespace nexuslab::sim
