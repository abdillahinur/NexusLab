// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <limits>
#include <stdexcept>

namespace nexuslab::transport {
std::uint64_t TransportRuntime::capacity_revision() const noexcept { return capacity_revision_; }
void TransportRuntime::reserve_capacity(topology::LinkId link, BitsPerSecond minimum) {
    if (!chunks_.empty() || minimum.value() == 0) {
        throw std::invalid_argument{
            "capacity reservation requires bootstrap and positive bandwidth"};
    }
    for (const auto direction : {topology::LinkDirection::AToB, topology::LinkDirection::BToA}) {
        const topology::DirectedLinkId id{link, direction};
        const auto current = require_service(id).queue().configuration().bandwidth;
        if (minimum >= current) {
            throw std::invalid_argument{"degraded capacity must be below both baselines"};
        }
    }
    for (const auto direction : {topology::LinkDirection::AToB, topology::LinkDirection::BToA}) {
        const topology::DirectedLinkId id{link, direction};
        const auto found = minimum_bandwidth_.find(id);
        if (found == minimum_bandwidth_.end() || minimum < found->second) {
            minimum_bandwidth_.insert_or_assign(id, minimum);
        }
    }
}
void TransportRuntime::handle_capacity_change(const LinkCapacityChangeEvent& event,
                                              sim::SimulationContext& /*context*/) {
    auto& forward = require_service({event.link, topology::LinkDirection::AToB});
    auto& reverse = require_service({event.link, topology::LinkDirection::BToA});
    if (event.forward.value() == 0 || event.reverse.value() == 0) {
        throw std::invalid_argument{"capacity must be positive"};
    }
    if (forward.queue().configuration().bandwidth == event.forward &&
        reverse.queue().configuration().bandwidth == event.reverse) {
        return;
    }
    if (capacity_revision_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{"capacity revision exhausted"};
    }
    forward.set_bandwidth(event.forward);
    reverse.set_bandwidth(event.reverse);
    ++capacity_revision_;
}

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
            const LinkStatistics before = service.statistics(context.now());
            const auto drained = service.reconcile_down(context);
            const LinkStatistics after = service.statistics(context.now());
            mark_dropped_link_down(drained, id, context);
            emit_link_metrics(id, before, after, context);
            emit_queue_gauges(id, service.queue().snapshot(), context);
        }
    }
}
} // namespace nexuslab::transport
