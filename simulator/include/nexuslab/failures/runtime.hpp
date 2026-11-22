// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nexuslab/sim/event.hpp"
#include "nexuslab/telemetry/records.hpp"
#include "nexuslab/traffic/model.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <array>
#include <map>
#include <set>
#include <utility>
#include <variant>

namespace nexuslab::sim {
class Simulation;
}
namespace nexuslab::collective {
class RingExecutor;
struct NoRouteAdmission;
} // namespace nexuslab::collective
namespace nexuslab::workload {
class WorkloadEngine;
}
namespace nexuslab::failures {
enum class Kind : std::uint8_t {
    LinkDown = 1,
    LinkDegraded = 2,
    PortDown = 3,
    SwitchDown = 4,
    GpuDown = 5
};
using Target =
    std::variant<topology::LinkId, topology::PortId, topology::SwitchId, topology::GpuId>;
struct Incident final {
    Target target;
    sim::SimTimeNs onset;
    std::optional<sim::SimTimeNs> recovery;
    Kind kind{Kind::LinkDown};
    std::optional<transport::BitsPerSecond> bandwidth{std::nullopt};
    bool operator==(const Incident&) const = default;
};
using LinkIncident = Incident;
struct Script final {
    std::vector<Incident> incidents;
    std::vector<traffic::Source> loads;
    Script() = default;
    explicit Script(std::vector<Incident> incident_specs,
                    std::vector<traffic::Source> load_specs = {})
        : incidents{std::move(incident_specs)}, loads{std::move(load_specs)} {}
    bool operator==(const Script&) const = default;
};
[[nodiscard]] std::uint64_t target_value(const Target& target);
[[nodiscard]] std::string_view kind_name(Kind kind);
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
    std::uint64_t capacity_revision{0};
    bool operator==(const Transition&) const = default;
};
struct IncidentSnapshot final {
    telemetry::FailureId id;
    Incident specification;
    bool applied{false};
    bool recovered{false};
    bool effective_operational{true};
    transport::TrafficCount dropped{};
    std::vector<workload::JobId> affected_jobs;
    std::uint64_t capacity_revision{0};
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
    std::uint32_t outcome_version{2};
    std::uint64_t capacity_revision{0};
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
            workload::WorkloadEngine* jobs = nullptr, Limits limits = {},
            telemetry::TelemetrySink telemetry = {});
    ~Runtime() override;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    void schedule(sim::Simulation& simulation);
    void handle(const transport::LinkStateChangeEvent& event, sim::SimulationContext& context);
    void handle(const transport::PortStateChangeEvent& event, sim::SimulationContext& context);
    void handle(const transport::SwitchStateChangeEvent& event, sim::SimulationContext& context);
    void handle(const transport::LinkCapacityChangeEvent& event, sim::SimulationContext& context);
    void handle(const workload::WorkloadEvent& event, sim::SimulationContext& context);
    [[nodiscard]] bool owns(sim::EventId event) const;
    [[nodiscard]] Report report() const;
    void attribute_no_route(const collective::NoRouteAdmission& admission);

  private:
    void on_down_drop(const transport::DownDrop& drop) override;
    using ResourceKey = std::pair<unsigned, std::uint64_t>;
    [[nodiscard]] static ResourceKey key(const Incident& incident);
    [[nodiscard]] bool operational(const Incident& incident) const;
    [[nodiscard]] sim::EventPayload event(const Action& action) const;
    void apply(const sim::EventPayload& payload, sim::SimulationContext& context);
    void process(const sim::EventPayload& payload, sim::SimulationContext& context);
    void affect_job(std::size_t index, workload::JobId job, sim::SimTimeNs now);
    void emit(std::size_t index, telemetry::FailureTransition kind, sim::SimTimeNs now,
              telemetry::Correlation correlation = {}, std::uint64_t bytes = 0);
    void refresh(sim::SimTimeNs now);
    void emit_counter(std::size_t index, telemetry::MetricId metric, std::uint64_t amount,
                      sim::SimTimeNs now);
    telemetry::TelemetrySink telemetry_;
    telemetry::Correlation action_correlation_;
    [[nodiscard]] std::size_t blocking_incident(const topology::PhysicalLink& link) const;
    void transition(std::size_t index, telemetry::FailureTransition kind, sim::SimTimeNs now);
    topology::TopologyGraph* graph_;
    transport::TransportRuntime* transport_;
    const collective::RingExecutor* ring_;
    workload::WorkloadEngine* jobs_;
    Limits limits_;
    Report report_;
    std::vector<Action> calendar_;
    std::map<sim::EventId, Action> pending_;
    std::map<ResourceKey, std::size_t> active_;
    std::map<topology::LinkId, std::array<transport::BitsPerSecond, 2>> baseline_;
    std::set<transport::ChunkId> attributed_;
    std::set<std::pair<telemetry::FailureId, workload::JobId>> affected_;
    bool scheduled_{false};
};
} // namespace nexuslab::failures
