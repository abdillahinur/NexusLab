// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/deterministic_rng.hpp"

#include <stdexcept>

namespace nexuslab::sim {

DeterministicRng::DeterministicRng(std::uint64_t seed) : engine_{seed} {}

std::uint64_t DeterministicRng::next_u64() noexcept {
    ++draw_count_;
    return engine_();
}

std::uint64_t DeterministicRng::uniform_below(std::uint64_t upper_exclusive) {
    if (upper_exclusive == 0) {
        throw std::invalid_argument{"uniform_below requires a nonzero upper bound"};
    }

    const std::uint64_t rejection_threshold = (0 - upper_exclusive) % upper_exclusive;
    while (true) {
        const std::uint64_t value = next_u64();
        if (value >= rejection_threshold) {
            return value % upper_exclusive;
        }
    }
}

std::uint64_t DeterministicRng::draw_count() const noexcept { return draw_count_; }

} // namespace nexuslab::sim
