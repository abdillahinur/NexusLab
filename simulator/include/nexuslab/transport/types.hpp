// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nexuslab::transport {

template <typename Tag> class StrongUnsigned final {
  public:
    explicit constexpr StrongUnsigned(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    auto operator<=>(const StrongUnsigned&) const = default;

  private:
    std::uint64_t value_;
};

struct TransferIdTag final {};
struct ChunkIdTag final {};
struct ByteCountTag final {};
struct BitsPerSecondTag final {};

using TransferId = StrongUnsigned<TransferIdTag>;
using ChunkId = StrongUnsigned<ChunkIdTag>;
using ByteCount = StrongUnsigned<ByteCountTag>;
using BitsPerSecond = StrongUnsigned<BitsPerSecondTag>;

template <typename Id> class SequentialTransportIdGenerator final {
  public:
    explicit SequentialTransportIdGenerator(std::uint64_t next_value = 0) noexcept
        : next_value_{next_value} {}
    SequentialTransportIdGenerator(const SequentialTransportIdGenerator&) = delete;
    SequentialTransportIdGenerator& operator=(const SequentialTransportIdGenerator&) = delete;
    SequentialTransportIdGenerator(SequentialTransportIdGenerator&&) = delete;
    SequentialTransportIdGenerator& operator=(SequentialTransportIdGenerator&&) = delete;

    [[nodiscard]] bool can_generate(std::uint64_t count) const noexcept {
        if (count == 0) {
            return true;
        }
        if (exhausted_) {
            return false;
        }
        return count - 1 <= std::numeric_limits<std::uint64_t>::max() - next_value_;
    }

    [[nodiscard]] Id next() {
        if (!can_generate(1)) {
            throw std::overflow_error{"transport ID sequence exhausted"};
        }

        const Id result{next_value_};
        if (next_value_ == std::numeric_limits<std::uint64_t>::max()) {
            exhausted_ = true;
        } else {
            ++next_value_;
        }
        return result;
    }

    void advance_past(Id id) noexcept {
        if (exhausted_ || id.value() < next_value_) {
            return;
        }
        if (id.value() == std::numeric_limits<std::uint64_t>::max()) {
            exhausted_ = true;
        } else {
            next_value_ = id.value() + 1;
        }
    }

  private:
    std::uint64_t next_value_;
    bool exhausted_{false};
};

} // namespace nexuslab::transport
