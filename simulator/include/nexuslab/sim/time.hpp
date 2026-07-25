// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

namespace nexuslab::sim {

class SimDurationNs final {
  public:
    constexpr SimDurationNs() noexcept = default;
    explicit constexpr SimDurationNs(std::uint64_t count) noexcept : count_{count} {}

    [[nodiscard]] constexpr std::uint64_t count() const noexcept { return count_; }

    auto operator<=>(const SimDurationNs&) const = default;

  private:
    std::uint64_t count_{0};
};

class SimTimeNs final {
  public:
    constexpr SimTimeNs() noexcept = default;
    explicit constexpr SimTimeNs(std::uint64_t count) noexcept : count_{count} {}

    [[nodiscard]] constexpr std::uint64_t count() const noexcept { return count_; }

    auto operator<=>(const SimTimeNs&) const = default;

  private:
    std::uint64_t count_{0};
};

enum class SimTimeUnit : std::uint8_t {
    Nanoseconds,
    Microseconds,
    Milliseconds,
    Seconds,
};

[[nodiscard]] constexpr std::optional<SimDurationNs>
checked_duration_from(std::uint64_t count, SimTimeUnit unit) noexcept {
    std::uint64_t multiplier{0};
    switch (unit) {
    case SimTimeUnit::Nanoseconds:
        multiplier = 1;
        break;
    case SimTimeUnit::Microseconds:
        multiplier = 1'000;
        break;
    case SimTimeUnit::Milliseconds:
        multiplier = 1'000'000;
        break;
    case SimTimeUnit::Seconds:
        multiplier = 1'000'000'000;
        break;
    }

    if (multiplier == 0 || count > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return SimDurationNs{count * multiplier};
}

[[nodiscard]] constexpr std::optional<SimTimeNs> checked_add(SimTimeNs time,
                                                             SimDurationNs duration) noexcept {
    if (duration.count() > std::numeric_limits<std::uint64_t>::max() - time.count()) {
        return std::nullopt;
    }
    return SimTimeNs{time.count() + duration.count()};
}

[[nodiscard]] constexpr std::optional<SimDurationNs> checked_difference(SimTimeNs end,
                                                                        SimTimeNs begin) noexcept {
    if (end < begin) {
        return std::nullopt;
    }
    return SimDurationNs{end.count() - begin.count()};
}

} // namespace nexuslab::sim
