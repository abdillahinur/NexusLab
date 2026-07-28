// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/transport/events.hpp"
#include "nexuslab/transport/link_queue.hpp"

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

  private:
    [[nodiscard]] sim::EventId schedule_completion(const TransferChunk& chunk,
                                                   sim::SimulationContext& context);

    DirectedLinkQueue queue_;
    std::optional<sim::EventId> scheduled_completion_;
};

} // namespace nexuslab::transport
