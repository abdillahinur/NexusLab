// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/workload/engine.hpp"
#include <stdexcept>
namespace nexuslab::workload {
// Dedicated training composition: this dispatcher exclusively consumes runtime outcomes.
class TrainingDispatcher final {
  public:
    TrainingDispatcher(WorkloadEngine& jobs, collective::RingExecutor& collectives,
                       transport::TransportRuntime& transport)
        : jobs_{jobs}, collectives_{collectives}, transport_{transport} {}
    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected no-op in training simulation"};
    }
    void operator()(const WorkloadEvent& e, sim::SimulationContext& c) {
        jobs_.handle(e, c);
        pump(c);
    }
    void operator()(const collective::LocalCompletionEvent& e, sim::SimulationContext& c) {
        collectives_.handle(e, c);
        pump(c);
    }
    void operator()(const transport::ChunkArrivalEvent& e, sim::SimulationContext& c) {
        transport_.handle_arrival(e, c);
        pump(c);
    }
    void operator()(const transport::TransmissionCompleteEvent& e, sim::SimulationContext& c) {
        transport_.handle_completion(e, c);
        pump(c);
    }
    void operator()(const transport::LinkStateChangeEvent& e, sim::SimulationContext& c) {
        transport_.handle_link_state_change(e, c);
        pump(c);
    }
    void operator()(const transport::PortStateChangeEvent& e, sim::SimulationContext& c) {
        transport_.handle_port_state_change(e, c);
        pump(c);
    }
    void operator()(const transport::SwitchStateChangeEvent& e, sim::SimulationContext& c) {
        transport_.handle_switch_state_change(e, c);
        pump(c);
    }

  private:
    void pump(sim::SimulationContext& context) {
        for (const auto& completion : transport_.take_completed_transfers()) {
            collectives_.handle(completion, context);
        }
        for (;;) {
            const auto completed = collectives_.take_completed();
            if (completed.empty()) {
                break;
            }
            for (const auto& completion : completed) {
                jobs_.handle(completion, context);
            }
        }
    }
    WorkloadEngine& jobs_;
    collective::RingExecutor& collectives_;
    transport::TransportRuntime& transport_;
};
} // namespace nexuslab::workload
