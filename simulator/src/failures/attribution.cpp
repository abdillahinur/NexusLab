// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/workload/engine.hpp"
#include <algorithm>
#include <deque>
namespace nexuslab::failures {
namespace {
using Distances = std::map<topology::NodeId, std::size_t>;
// Structural fabric distances deliberately ignore current health. Both directions exist.
Distances healthy_distances(const topology::TopologyGraph& graph, topology::NodeId start) {
    Distances distances{{start, 0}};
    std::deque<topology::NodeId> pending{start};
    while (!pending.empty()) {
        const auto node = pending.front();
        pending.pop_front();
        const auto distance = distances.at(node);
        for (const auto& arc : graph.outgoing(node)) {
            if (graph.find(arc.id.link)->kind != topology::LinkKind::Fabric) {
                continue;
            }
            const auto next = graph.find(arc.destination)->owner;
            if (distances.emplace(next, distance + 1).second) {
                pending.push_back(next);
            }
        }
    }
    return distances;
}
bool minimum_arc(topology::NodeId from, topology::NodeId to, const Distances& source,
                 const Distances& destination, std::size_t hops) {
    const auto left = source.find(from);
    const auto right = destination.find(to);
    return left != source.end() && right != destination.end() && left->second < hops &&
           right->second == hops - left->second - 1;
}
} // namespace
void Runtime::attribute_no_route(const collective::NoRouteAdmission& admission) {
    if (jobs_ == nullptr || active_.empty()) {
        return;
    }
    const auto job = jobs_->owner(admission.collective);
    if (!job.has_value()) {
        return;
    }
    const auto snapshot = jobs_->snapshot(*job, admission.timestamp);
    if (!snapshot.has_value() || workload::terminal(snapshot->state)) {
        return;
    }
    const auto source = healthy_distances(*graph_, admission.endpoints.source);
    const auto destination = healthy_distances(*graph_, admission.endpoints.destination);
    const auto target = source.find(admission.endpoints.destination);
    if (target == source.end()) {
        return;
    }
    auto selected = report_.incidents.size();
    for (const auto& link : graph_->links()) {
        if (link.kind != topology::LinkKind::Fabric) {
            continue;
        }
        const auto a = graph_->find(link.endpoint_a)->owner;
        const auto b = graph_->find(link.endpoint_b)->owner;
        if (minimum_arc(a, b, source, destination, target->second) ||
            minimum_arc(b, a, source, destination, target->second)) {
            selected = std::min(selected, blocking_incident(link));
        }
    }
    if (selected != report_.incidents.size()) {
        affect_job(selected, *job, admission.timestamp);
        telemetry::Correlation correlation;
        correlation.job = job;
        correlation.collective = admission.collective;
        correlation.flow = admission.flow;
        correlation.event = admission.event;
        correlation.cause = admission.cause;
        emit(selected, telemetry::FailureTransition::AdmissionRejected, admission.timestamp,
             correlation);
    }
}
} // namespace nexuslab::failures
