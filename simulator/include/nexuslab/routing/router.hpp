// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/routing/policy.hpp"

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
           const PolicyRegistry& registry, RoutingConfiguration configuration = {});
    [[nodiscard]] std::optional<transport::SubmittedTransfer>
    submit(const RouteRequest& request, sim::SimulationContext& context);
    [[nodiscard]] std::span<const RouteDecision> decisions() const noexcept;
    [[nodiscard]] std::vector<RouteDecision> take_decisions();
    [[nodiscard]] CacheStatistics cache_statistics() const noexcept;

  private:
    const topology::TopologyGraph* graph_;
    transport::TransportRuntime* runtime_;
    RoutingConfiguration configuration_;
    std::unique_ptr<RoutingPolicy> policy_;
    PathService paths_;
    std::vector<RouteDecision> decisions_;
};
} // namespace nexuslab::routing
