// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nexuslab::topology {

template <typename Tag> class StrongId final {
  public:
    explicit constexpr StrongId(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    auto operator<=>(const StrongId&) const = default;

  private:
    std::uint64_t value_;
};

struct GpuIdTag final {};
struct NicIdTag final {};
struct SwitchIdTag final {};
struct RackIdTag final {};
struct PortIdTag final {};
struct LinkIdTag final {};

using GpuId = StrongId<GpuIdTag>;
using NicId = StrongId<NicIdTag>;
using SwitchId = StrongId<SwitchIdTag>;
using RackId = StrongId<RackIdTag>;
using PortId = StrongId<PortIdTag>;
using LinkId = StrongId<LinkIdTag>;

template <typename Id> class SequentialIdGenerator final {
  public:
    explicit SequentialIdGenerator(std::uint64_t next_value = 0) noexcept
        : next_value_{next_value} {}
    SequentialIdGenerator(const SequentialIdGenerator&) = delete;
    SequentialIdGenerator& operator=(const SequentialIdGenerator&) = delete;
    SequentialIdGenerator(SequentialIdGenerator&&) = delete;
    SequentialIdGenerator& operator=(SequentialIdGenerator&&) = delete;

    [[nodiscard]] Id next() {
        if (exhausted_) {
            throw std::overflow_error{"topology ID sequence exhausted"};
        }

        const Id result{next_value_};
        if (next_value_ == std::numeric_limits<std::uint64_t>::max()) {
            exhausted_ = true;
        } else {
            ++next_value_;
        }
        return result;
    }

  private:
    std::uint64_t next_value_;
    bool exhausted_{false};
};

} // namespace nexuslab::topology
