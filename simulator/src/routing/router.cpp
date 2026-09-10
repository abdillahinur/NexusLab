// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/routing/router.hpp"
#include "nexuslab/sim/simulation.hpp"
#include <limits>
#include <stdexcept>
#include <utility>

namespace nexuslab::routing {
Router::Router(const topology::TopologyGraph& graph, transport::TransportRuntime& runtime,
               const PolicyRegistry& registry, RoutingConfiguration configuration,
               telemetry::TelemetrySink telemetry)
    : graph_{&graph}, runtime_{&runtime}, configuration_{std::move(configuration)},
      policy_{registry.create(configuration_.policy)}, paths_{graph, configuration_.paths},
      telemetry_{telemetry} {
    if (configuration_.maximum_decisions == 0) {
        throw std::invalid_argument{"routing decision limit must be positive"};
    }
}
std::optional<transport::SubmittedTransfer> Router::submit(const RouteRequest& request,
                                                           sim::SimulationContext& context) {
    if (request.bytes.value() == 0 || request.maximum_chunk_bytes.value() == 0) {
        throw std::invalid_argument{"routing requires nonzero transfer quantities"};
    }
    if (decisions_.size() == configuration_.maximum_decisions) {
        throw std::length_error{"drain routing decisions before submitting more work"};
    }
    const auto& candidates = paths_.lookup(request.endpoints);
    RouteDecision decision{request,
                           std::string{policy_->name()},
                           policy_->version(),
                           context.now(),
                           graph_->operational_revision(),
                           candidates.size(),
                           {},
                           0,
                           "no operational fabric path",
                           std::nullopt};
    if (candidates.empty()) {
        const telemetry::RoutingDecisionId id = next_decision_id();
        emit_decision(decision, telemetry::RoutingDecisionOutcome::NoRoute, id, context);
        decisions_.push_back(std::move(decision));
        return std::nullopt;
    }
    const auto choice =
        policy_->choose({request, configuration_.seed, candidates, FabricView{*runtime_}});
    if (choice.candidate >= candidates.size()) {
        throw std::invalid_argument{"routing policy returned invalid candidate index"};
    }
    decision.path = candidates[choice.candidate];
    decision.score = choice.score;
    decision.reason = choice.reason;
    auto transfer =
        runtime_->submit_transfer({request.endpoints.source, request.endpoints.destination,
                                   request.bytes, request.maximum_chunk_bytes, decision.path},
                                  context);
    decision.transfer = transfer.id;
    const telemetry::RoutingDecisionId id = next_decision_id();
    emit_decision(decision, telemetry::RoutingDecisionOutcome::Selected, id, context);
    decisions_.push_back(std::move(decision));
    return transfer;
}

telemetry::RoutingDecisionId Router::next_decision_id() {
    if (decision_ids_exhausted_) {
        throw std::overflow_error{"routing decision ID sequence exhausted"};
    }
    const telemetry::RoutingDecisionId id{next_decision_id_};
    if (next_decision_id_ == std::numeric_limits<std::uint64_t>::max()) {
        decision_ids_exhausted_ = true;
    } else {
        ++next_decision_id_;
    }
    return id;
}

void Router::emit_decision(const RouteDecision& decision, telemetry::RoutingDecisionOutcome outcome,
                           telemetry::RoutingDecisionId id, sim::SimulationContext& context) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation;
    correlation.event = context.current_event_id();
    correlation.cause = context.cause();
    correlation.transfer = decision.transfer;
    correlation.routing_decision = id;
    telemetry_.record(context.now(), correlation,
                      telemetry::RoutingDecisionObservation{
                          id, decision.policy, decision.version, decision.operational_revision,
                          decision.candidates, decision.path, decision.score, decision.reason,
                          decision.transfer, outcome});
    telemetry_.record(
        context.now(), correlation,
        telemetry::CounterObservation{
            telemetry::MetricId::RoutingDecisionTotal,
            {{telemetry::MetricLabel::Policy, telemetry::policy_label_value(decision.policy)},
             {telemetry::MetricLabel::Outcome, static_cast<std::uint64_t>(outcome)}},
            1});
}
std::span<const RouteDecision> Router::decisions() const noexcept { return decisions_; }
std::vector<RouteDecision> Router::take_decisions() { return std::exchange(decisions_, {}); }
CacheStatistics Router::cache_statistics() const noexcept { return paths_.statistics(); }
} // namespace nexuslab::routing
