// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/workload/model.hpp"
#include <limits>
#include <stdexcept>
namespace nexuslab::workload {
std::uint64_t checked_sum(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error{"workload quantity overflow"};
    }
    return left + right;
}
std::uint64_t checked_product(std::uint64_t left, std::uint64_t right) {
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right) {
        throw std::overflow_error{"workload quantity overflow"};
    }
    return left * right;
}
bool terminal(JobState state) noexcept {
    return state == JobState::Succeeded || state == JobState::Failed ||
           state == JobState::Cancelled;
}
} // namespace nexuslab::workload
