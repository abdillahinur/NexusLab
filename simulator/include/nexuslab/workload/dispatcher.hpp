// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/traffic/runtime.hpp"
#include "nexuslab/workload/engine.hpp"
#include <stdexcept>
namespace nexuslab::workload {
// Dedicated training composition: this dispatcher exclusively consumes runtime outcomes.
class TrainingDispatcher final {
  public:
    TrainingDispatcher(WorkloadEngine& jobs, collective::RingExecutor& collectives,
                       transport::TransportRuntime& transport,
                       failures::Runtime* failures = nullptr, traffic::Runtime* loads = nullptr)
        : jobs_{jobs}, collectives_{collectives}, transport_{transport}, failures_{failures},
          loads_{loads} {}
    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected no-op in training simulation"};
    }
    void operator()(const WorkloadEvent& e, sim::SimulationContext& c) {
        if (failures_ != nullptr && failures_->owns(c.current_event_id())) {
            failures_->handle(e, c);
        } else {
            jobs_.handle(e, c);
        }
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
        if (failures_ != nullptr) {
            failures_->handle(e, c);
        } else {
            transport_.handle_link_state_change(e, c);
        }
        pump(c);
    }
    void operator()(const transport::PortStateChangeEvent& e, sim::SimulationContext& c) {
        if (failures_ != nullptr) {
            failures_->handle(e, c);
        } else {
            transport_.handle_port_state_change(e, c);
        }
        pump(c);
    }
    void operator()(const transport::SwitchStateChangeEvent& e, sim::SimulationContext& c) {
        if (failures_ != nullptr) {
            failures_->handle(e, c);
        } else {
            transport_.handle_switch_state_change(e, c);
        }
        pump(c);
    }
    void operator()(const transport::LinkCapacityChangeEvent& e, sim::SimulationContext& c) {
        if (failures_ != nullptr) {
            failures_->handle(e, c);
        } else {
            transport_.handle_capacity_change(e, c);
        }
        pump(c);
    }

    void operator()(const traffic::InjectionEvent& event, sim::SimulationContext& context) {
        if (loads_ == nullptr) {
            throw std::logic_error{"unexpected background injection"};
        }
        loads_->handle(event, context);
        pump(context);
    }

  private:
    void pump(sim::SimulationContext& context) {
        for (const auto& completion : transport_.take_completed_transfers()) {
            if (loads_ != nullptr && loads_->owns(completion.transfer)) {
                loads_->handle(completion);
            } else {
                collectives_.handle(completion, context);
            }
        }
        for (;;) {
            jobs_.dispatch_waiting(context);
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
    failures::Runtime* failures_;
    traffic::Runtime* loads_;
};
} // namespace nexuslab::workload
