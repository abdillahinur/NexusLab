// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nexuslab/telemetry/records.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <map>
#include <set>

namespace nexuslab::sim {
class Simulation;
}
namespace nexuslab::collective {
class RingExecutor;
}
namespace nexuslab::workload {
class WorkloadEngine;
}
namespace nexuslab::failures {
struct LinkIncident final {
    topology::LinkId target;
    sim::SimTimeNs onset;
    std::optional<sim::SimTimeNs> recovery;
    bool operator==(const LinkIncident&) const = default;
};
struct Script final {
    std::vector<LinkIncident> incidents;
    bool operator==(const Script&) const = default;
};
struct Limits final {
    std::size_t incidents{10'000};
    std::size_t attributed_chunks{1'000'000};
    std::size_t affected_jobs{1'000'000};
};
struct Action final {
    sim::SimTimeNs timestamp;
    bool recovery;
    std::size_t incident;
    bool operator==(const Action&) const = default;
};
struct Transition final {
    telemetry::FailureId incident;
    telemetry::FailureTransition kind;
    sim::SimTimeNs timestamp;
    bool effective_operational;
    bool operator==(const Transition&) const = default;
};
struct IncidentSnapshot final {
    telemetry::FailureId id;
    LinkIncident specification;
    bool applied{false};
    bool recovered{false};
    bool effective_operational{true};
    transport::TrafficCount dropped{};
    std::vector<workload::JobId> affected_jobs;
    bool operator==(const IncidentSnapshot&) const = default;
};
struct Drop final {
    telemetry::FailureId incident;
    transport::DownDrop observation;
    std::optional<workload::JobId> job;
    bool operator==(const Drop&) const = default;
};
struct Report final {
    std::uint64_t script_digest;
    std::vector<IncidentSnapshot> incidents;
    std::vector<Transition> timeline;
    std::vector<Drop> drops;
    bool operator==(const Report&) const = default;
};
// Normalizes and validates without registering events or mutating domain state.
[[nodiscard]] std::vector<Action>
validate(const Script& script, const topology::TopologyGraph& graph, Limits limits = {});
[[nodiscard]] std::uint64_t script_digest(const Script& script,
                                          const topology::TopologyGraph& graph);
[[nodiscard]] std::string_view transition_name(telemetry::FailureTransition transition);
// Must be destroyed before its borrowed domain owners; disconnects its observer on destruction.
class Runtime final : private transport::DownDropObserver {
  public:
    Runtime(const Script& script, topology::TopologyGraph& graph,
            transport::TransportRuntime& transport, const collective::RingExecutor* ring = nullptr,
            const workload::WorkloadEngine* jobs = nullptr, Limits limits = {});
    ~Runtime() override;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    void schedule(sim::Simulation& simulation);
    void handle(const transport::LinkStateChangeEvent& event, sim::SimulationContext& context);
    [[nodiscard]] Report report() const;

  private:
    void on_down_drop(const transport::DownDrop& drop) override;
    [[nodiscard]] bool operational(topology::LinkId link) const;
    void transition(std::size_t index, telemetry::FailureTransition kind, sim::SimTimeNs now);
    topology::TopologyGraph* graph_;
    transport::TransportRuntime* transport_;
    const collective::RingExecutor* ring_;
    const workload::WorkloadEngine* jobs_;
    Limits limits_;
    Report report_;
    std::vector<Action> calendar_;
    std::map<sim::EventId, Action> pending_;
    std::map<topology::LinkId, std::size_t> active_;
    std::set<transport::ChunkId> attributed_;
    std::set<std::pair<telemetry::FailureId, workload::JobId>> affected_;
    bool scheduled_{false};
};
} // namespace nexuslab::failures
