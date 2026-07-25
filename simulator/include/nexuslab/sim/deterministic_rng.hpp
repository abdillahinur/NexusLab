// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <random>

namespace nexuslab::sim {

class DeterministicRng final {
  public:
    explicit DeterministicRng(std::uint64_t seed);

    [[nodiscard]] std::uint64_t next_u64() noexcept;
    [[nodiscard]] std::uint64_t uniform_below(std::uint64_t upper_exclusive);
    [[nodiscard]] std::uint64_t draw_count() const noexcept;

  private:
    std::mt19937_64 engine_;
    std::uint64_t draw_count_{0};
};

} // namespace nexuslab::sim
