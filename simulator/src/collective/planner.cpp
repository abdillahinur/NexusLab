// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/collective/model.hpp"
#include <limits>
#include <stdexcept>
namespace nexuslab::collective {
std::string_view phase_name(Phase phase) {
    switch (phase) {
    case Phase::ReduceScatter:
        return "reduce_scatter";
    case Phase::AllGather:
        return "all_gather";
    case Phase::Succeeded:
        return "succeeded";
    case Phase::Failed:
        return "failed";
    case Phase::Cancelled:
        return "cancelled";
    }
    throw std::invalid_argument{"unknown collective phase"};
}
std::uint64_t planned_volume(std::uint32_t participants, transport::ByteCount bytes) {
    if (participants == 0 || participants > 8192 || bytes.value() == 0) {
        throw std::invalid_argument{"invalid ring dimensions"};
    }
    const std::uint64_t factor = 2ULL * (participants - 1);
    if (factor != 0 && bytes.value() > std::numeric_limits<std::uint64_t>::max() / factor) {
        throw std::overflow_error{"ring volume overflow"};
    }
    return factor * bytes.value();
}
std::vector<RingTransfer> plan_round(std::uint32_t participants, transport::ByteCount bytes,
                                     Phase phase, std::uint32_t round) {
    static_cast<void>(planned_volume(participants, bytes));
    if ((phase != Phase::ReduceScatter && phase != Phase::AllGather) || participants < 2 ||
        round >= participants - 1) {
        throw std::invalid_argument{"invalid ring phase or round"};
    }
    std::vector<RingTransfer> result;
    result.reserve(participants);
    for (std::uint32_t rank = 0; rank < participants; ++rank) {
        const auto shard =
            (rank + participants - round + static_cast<std::uint32_t>(phase == Phase::AllGather)) %
            participants;
        const auto size = bytes.value() / participants +
                          static_cast<std::uint64_t>(shard < bytes.value() % participants);
        result.push_back({rank, (rank + 1) % participants, shard, transport::ByteCount{size}});
    }
    return result;
}
} // namespace nexuslab::collective
