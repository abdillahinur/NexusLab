// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>

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

} // namespace nexuslab::transport
