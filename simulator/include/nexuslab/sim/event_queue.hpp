// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event.hpp"

#include <cstddef>
#include <optional>
#include <queue>
#include <vector>

namespace nexuslab::sim {

class EventQueue final {
  public:
    void push(Event event);

    [[nodiscard]] std::optional<Event> pop();
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct LaterEvent final {
        [[nodiscard]] bool operator()(const Event& left, const Event& right) const noexcept;
    };

    std::priority_queue<Event, std::vector<Event>, LaterEvent> events_;
};

} // namespace nexuslab::sim
