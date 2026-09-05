// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <stdexcept>

namespace nexuslab::transport {
namespace {
void validate_state(topology::OperationalState state) {
    if (state != topology::OperationalState::Up && state != topology::OperationalState::Down) {
        throw std::invalid_argument{"invalid operational state"};
    }
}
} // namespace

sim::EventId TransportRuntime::schedule_port_state_change(topology::PortId port,
                                                          topology::OperationalState state,
                                                          sim::SimTimeNs timestamp,
                                                          sim::SimulationContext& context) {
    validate_state(state);
    if (topology_->find(port) == nullptr) {
        throw std::invalid_argument{"unknown port state-change target"};
    }
    return context.schedule(
        {timestamp, sim::EventPriority::Critical, PortStateChangeEvent{port, state}});
}

sim::EventId TransportRuntime::schedule_switch_state_change(topology::SwitchId network_switch,
                                                            topology::OperationalState state,
                                                            sim::SimTimeNs timestamp,
                                                            sim::SimulationContext& context) {
    validate_state(state);
    if (topology_->find(network_switch) == nullptr) {
        throw std::invalid_argument{"unknown switch state-change target"};
    }
    return context.schedule(
        {timestamp, sim::EventPriority::Critical, SwitchStateChangeEvent{network_switch, state}});
}

void TransportRuntime::handle_port_state_change(const PortStateChangeEvent& event,
                                                sim::SimulationContext& context) {
    validate_state(event.state);
    if (!topology_->set_port_state(event.port, event.state)) {
        throw std::invalid_argument{"unknown port state-change target"};
    }
    reconcile_unavailable(context);
}

void TransportRuntime::handle_switch_state_change(const SwitchStateChangeEvent& event,
                                                  sim::SimulationContext& context) {
    validate_state(event.state);
    if (!topology_->set_switch_state(event.network_switch, event.state)) {
        throw std::invalid_argument{"unknown switch state-change target"};
    }
    reconcile_unavailable(context);
}

void TransportRuntime::reconcile_unavailable(sim::SimulationContext& context) {
    for (auto& [id, service] : services_) {
        if (!topology_->is_operational(require_fabric_arc(id))) {
            const auto drained = service.reconcile_down(context);
            mark_dropped_link_down(drained, id, context.now());
        }
    }
}
} // namespace nexuslab::transport
