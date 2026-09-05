// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event.hpp"

#include <functional>
#include <stdexcept>
#include <utility>

namespace nexuslab::sim {
class SimulationContext;
}

namespace nexuslab::test {

template <typename Handler> class NoOpDispatcher final {
  public:
    explicit NoOpDispatcher(Handler handler) : handler_{std::move(handler)} {}

    void operator()(const sim::NoOpEvent& event, sim::SimulationContext& context) {
        std::invoke(handler_, event, context);
    }

    void operator()(const transport::ChunkArrivalEvent& event, sim::SimulationContext& context) {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"unexpected chunk-arrival event in no-op dispatcher"};
    }

    void operator()(const transport::TransmissionCompleteEvent& event,
                    sim::SimulationContext& context) {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"unexpected transmission-completion event in no-op dispatcher"};
    }

    void operator()(const transport::LinkStateChangeEvent& event, sim::SimulationContext& context) {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"unexpected link-state event in no-op dispatcher"};
    }

    void operator()(const nexuslab::transport::PortStateChangeEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected port-state event in this dispatcher"};
    }

    void operator()(const nexuslab::transport::SwitchStateChangeEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected switch-state event in this dispatcher"};
    }

  private:
    Handler handler_;
};

template <typename Handler> NoOpDispatcher(Handler) -> NoOpDispatcher<Handler>;

} // namespace nexuslab::test
