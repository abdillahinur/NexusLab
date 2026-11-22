// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/routing/router.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/traffic/events.hpp"
#include "nexuslab/traffic/model.hpp"
namespace nexuslab::traffic {
class Runtime final {
  public:
    Runtime(std::vector<Source> sources, const topology::TopologyGraph& graph,
            routing::Router& router, Limits limits = {}, telemetry::TelemetrySink telemetry = {});
    void schedule(sim::Simulation& simulation);
    void handle(const InjectionEvent& event, sim::SimulationContext& context);
    void handle(const transport::TransferCompletion& completion);
    [[nodiscard]] bool owns(transport::TransferId transfer) const;
    [[nodiscard]] const std::vector<Request>& report() const noexcept;

  private:
    void emit(const Request& request, sim::SimTimeNs now, telemetry::Correlation correlation = {});
    telemetry::TelemetrySink telemetry_;
    std::vector<Source> sources_;
    const topology::TopologyGraph* graph_;
    routing::Router* router_;
    std::vector<Request> requests_;
    std::map<transport::TransferId, std::size_t> pending_;
    std::map<sim::EventId, std::size_t> injections_;
    bool scheduled_{false};
};
} // namespace nexuslab::traffic
