// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/routing/policy.hpp"
#include "nexuslab/telemetry/session.hpp"

namespace nexuslab::routing {
struct RoutingConfiguration final {
    std::string policy{"ecmp"};
    std::uint64_t seed{42};
    PathLimits paths{};
    std::size_t maximum_decisions{100'000};
};
struct RouteDecision final {
    RouteRequest request;
    std::string policy;
    std::uint64_t version;
    sim::SimTimeNs timestamp;
    std::uint64_t operational_revision;
    std::size_t candidates;
    Path path;
    std::uint64_t score;
    std::string reason;
    std::optional<transport::TransferId> transfer;
    bool operator==(const RouteDecision&) const = default;
};
class Router final {
  public:
    Router(const topology::TopologyGraph& graph, transport::TransportRuntime& runtime,
           const PolicyRegistry& registry, RoutingConfiguration configuration = {},
           telemetry::TelemetrySink telemetry = {});
    [[nodiscard]] std::optional<transport::SubmittedTransfer>
    submit(const RouteRequest& request, sim::SimulationContext& context);
    [[nodiscard]] std::span<const RouteDecision> decisions() const noexcept;
    [[nodiscard]] std::vector<RouteDecision> take_decisions();
    [[nodiscard]] CacheStatistics cache_statistics() const noexcept;

  private:
    [[nodiscard]] telemetry::RoutingDecisionId next_decision_id();
    void emit_decision(const RouteDecision& decision, telemetry::RoutingDecisionOutcome outcome,
                       telemetry::RoutingDecisionId id, sim::SimulationContext& context);
    const topology::TopologyGraph* graph_;
    transport::TransportRuntime* runtime_;
    RoutingConfiguration configuration_;
    std::unique_ptr<RoutingPolicy> policy_;
    PathService paths_;
    telemetry::TelemetrySink telemetry_;
    std::vector<RouteDecision> decisions_;
    std::uint64_t next_decision_id_{0};
    bool decision_ids_exhausted_{false};
};
} // namespace nexuslab::routing
