// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/transport/events.hpp"
#include "nexuslab/transport/link_queue.hpp"
#include "nexuslab/transport/statistics.hpp"

#include <optional>

namespace nexuslab::sim {
class SimulationContext;
}

namespace nexuslab::transport {

class DirectedLinkService final {
  public:
    explicit DirectedLinkService(DirectedLinkConfiguration configuration);

    [[nodiscard]] const DirectedLinkQueue& queue() const noexcept;
    [[nodiscard]] std::optional<sim::EventId> scheduled_completion() const noexcept;

    [[nodiscard]] AdmissionResult admit(TransferChunk chunk, sim::SimulationContext& context);
    [[nodiscard]] ServiceTransition handle_completion(const TransmissionCompleteEvent& event,
                                                      sim::SimulationContext& context);
    [[nodiscard]] QueueDrain reconcile_down(sim::SimulationContext& context);
    [[nodiscard]] LinkStatistics statistics(sim::SimTimeNs now) const;
    void record_link_down(const TransferChunk& chunk);

  private:
    [[nodiscard]] sim::EventId schedule_completion(const TransferChunk& chunk,
                                                   sim::SimulationContext& context);

    DirectedLinkQueue queue_;
    std::optional<sim::EventId> scheduled_completion_;
    LinkStatistics statistics_;
    std::optional<sim::SimTimeNs> busy_since_;
};

} // namespace nexuslab::transport
