// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/sim/time.hpp"
#include "nexuslab/topology/id.hpp"
#include "nexuslab/transport/types.hpp"
#include <string>
#include <string_view>
#include <vector>
namespace nexuslab::sim {
class SimulationContext;
}
namespace nexuslab::collective {
struct CollectiveIdTag final {};
using CollectiveId = topology::StrongId<CollectiveIdTag>;
enum class Phase : std::uint8_t {
    ReduceScatter = 1,
    AllGather = 2,
    Succeeded = 3,
    Failed = 4,
    Cancelled = 5
};
struct CollectiveRequest final {
    std::vector<topology::GpuId> participants;
    transport::ByteCount bytes;
    transport::ByteCount chunk_bytes;
};
struct RingTransfer final {
    std::uint32_t source;
    std::uint32_t destination;
    std::uint32_t shard;
    transport::ByteCount bytes;
    bool operator==(const RingTransfer&) const = default;
};
struct LocalCompletionEvent final {
    CollectiveId collective;
    std::uint32_t round;
    std::uint32_t rank;
    Phase phase;
    bool operator==(const LocalCompletionEvent&) const = default;
};
struct CollectiveResult final {
    CollectiveId id;
    Phase outcome;
    sim::SimTimeNs started;
    sim::SimTimeNs finished;
    std::uint64_t planned_bytes;
    std::uint64_t issued_fabric_bytes;
    std::uint64_t issued_local_bytes;
    std::uint64_t delivered_bytes;
    std::string reason;
    bool operator==(const CollectiveResult&) const = default;
};
struct CollectiveTimeline final {
    CollectiveId id;
    sim::SimTimeNs timestamp;
    Phase phase;
    std::uint32_t round;
    bool operator==(const CollectiveTimeline&) const = default;
};
class CollectiveExecutor {
  public:
    virtual ~CollectiveExecutor() = default;
    [[nodiscard]] virtual CollectiveId submit(const CollectiveRequest& request,
                                              sim::SimulationContext& context) = 0;
    virtual void cancel(CollectiveId id) = 0;
    [[nodiscard]] virtual std::vector<CollectiveResult> take_completed() = 0;
};
[[nodiscard]] std::vector<RingTransfer> plan_round(std::uint32_t participants,
                                                   transport::ByteCount bytes, Phase phase,
                                                   std::uint32_t round);
[[nodiscard]] std::string_view phase_name(Phase phase);
[[nodiscard]] std::uint64_t planned_volume(std::uint32_t participants, transport::ByteCount bytes);
} // namespace nexuslab::collective
