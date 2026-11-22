// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/topology/export.hpp"
#include "nexuslab/workload/engine.hpp"
#include <algorithm>
#include <tuple>

namespace nexuslab::failures {
std::string_view transition_name(telemetry::FailureTransition transition) {
    switch (transition) {
    case telemetry::FailureTransition::Scheduled:
        return "scheduled";
    case telemetry::FailureTransition::Applied:
        return "applied";
    case telemetry::FailureTransition::Detected:
        return "detected";
    case telemetry::FailureTransition::RecoveryStarted:
        return "recovery_started";
    case telemetry::FailureTransition::Recovered:
        return "recovered";
    }
    throw std::invalid_argument{"invalid failure transition"};
}
std::vector<Action> validate(const Script& script, const topology::TopologyGraph& graph,
                             Limits limits) {
    if (limits.incidents == 0 || limits.attributed_chunks == 0 || limits.affected_jobs == 0 ||
        script.incidents.size() > limits.incidents) {
        throw std::length_error{"failure script limit exceeded"};
    }
    std::vector<std::size_t> order;
    std::vector<Action> actions;
    for (std::size_t i = 0; i < script.incidents.size(); ++i) {
        const auto& incident = script.incidents[i];
        const auto* link = graph.find(incident.target);
        if (link == nullptr || link->kind != topology::LinkKind::Fabric ||
            link->state != topology::OperationalState::Up) {
            throw std::invalid_argument{"failure target must be an initially up fabric link"};
        }
        for (const auto& arc : topology::directed_links(*link)) {
            if (!graph.is_operational(arc)) {
                throw std::invalid_argument{"failure target is initially blocked"};
            }
        }
        if (incident.recovery.has_value() && *incident.recovery <= incident.onset) {
            throw std::invalid_argument{"failure recovery must follow onset"};
        }
        order.push_back(i);
        actions.push_back({incident.onset, false, i});
        if (incident.recovery.has_value()) {
            actions.push_back({*incident.recovery, true, i});
        }
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& x = script.incidents[a];
        const auto& y = script.incidents[b];
        return std::tie(x.target, x.onset) < std::tie(y.target, y.onset);
    });
    for (std::size_t i = 1; i < order.size(); ++i) {
        const auto& previous = script.incidents[order[i - 1]];
        const auto& next = script.incidents[order[i]];
        if (previous.target == next.target &&
            (!previous.recovery.has_value() || *previous.recovery > next.onset)) {
            throw std::invalid_argument{"overlapping failure episodes on the same target"};
        }
    }
    std::sort(actions.begin(), actions.end(), [](const Action& a, const Action& b) {
        return std::tuple{a.timestamp, !a.recovery, a.incident} <
               std::tuple{b.timestamp, !b.recovery, b.incident};
    });
    return actions;
}
std::uint64_t script_digest(const Script& script, const topology::TopologyGraph& graph) {
    static_cast<void>(validate(script, graph));
    std::uint64_t hash{14'695'981'039'346'656'037ULL};
    const auto byte = [&hash](unsigned char value) {
        hash = (hash ^ value) * 1'099'511'628'211ULL;
    };
    const auto integer = [&byte](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<unsigned char>((value >> shift) & 255U));
        }
    };
    const auto string = [&integer, &byte](std::string_view value) {
        integer(value.size());
        for (const char character : value) {
            byte(static_cast<unsigned char>(character));
        }
    };
    string("nexuslab.failure-script");
    integer(1);
    string(topology::serialize_topology_yaml(graph));
    integer(1);
    integer(script.incidents.size());
    for (std::size_t i = 0; i < script.incidents.size(); ++i) {
        const auto& incident = script.incidents[i];
        integer(i + 1);
        integer(1);
        integer(incident.target.value());
        integer(incident.onset.count());
        integer(incident.recovery.has_value() ? 1 : 0);
        if (incident.recovery.has_value()) {
            integer(incident.recovery->count());
        }
        integer(0);
    }
    integer(0);
    return hash;
}
Runtime::Runtime(const Script& script, topology::TopologyGraph& graph,
                 transport::TransportRuntime& transport, const collective::RingExecutor* ring,
                 const workload::WorkloadEngine* jobs, Limits limits)
    : graph_{&graph}, transport_{&transport}, ring_{ring}, jobs_{jobs}, limits_{limits},
      report_{script_digest(script, graph), {}, {}, {}},
      calendar_{validate(script, graph, limits)} {
    for (std::size_t i = 0; i < script.incidents.size(); ++i) {
        const auto& incident = script.incidents[i];
        for (const auto& arc : topology::directed_links(*graph.find(incident.target))) {
            if (transport.find_service(arc.id) == nullptr) {
                throw std::invalid_argument{"failure target requires both configured directions"};
            }
        }
        report_.incidents.push_back(
            {telemetry::FailureId{i + 1}, incident, false, false, true, {}, {}});
    }
    transport_->set_down_drop_observer(this);
}
Runtime::~Runtime() { transport_->set_down_drop_observer(nullptr); }
bool Runtime::operational(topology::LinkId link) const {
    const auto arcs = topology::directed_links(*graph_->find(link));
    return graph_->is_operational(arcs[0]) && graph_->is_operational(arcs[1]);
}
void Runtime::transition(std::size_t index, telemetry::FailureTransition kind, sim::SimTimeNs now) {
    auto& incident = report_.incidents.at(index);
    incident.effective_operational = operational(incident.specification.target);
    report_.timeline.push_back({incident.id, kind, now, incident.effective_operational});
}
void Runtime::schedule(sim::Simulation& simulation) {
    if (scheduled_ || simulation.now().count() != 0) {
        throw std::logic_error{"failure runtime must be scheduled once at bootstrap"};
    }
    scheduled_ = true;
    for (std::size_t i = 0; i < report_.incidents.size(); ++i) {
        transition(i, telemetry::FailureTransition::Scheduled, sim::SimTimeNs{0});
    }
    for (const auto& action : calendar_) {
        const auto& incident = report_.incidents[action.incident];
        const auto event = simulation.schedule(
            {action.timestamp, sim::EventPriority::Critical,
             transport::LinkStateChangeEvent{incident.specification.target,
                                             action.recovery ? topology::OperationalState::Up
                                                             : topology::OperationalState::Down}});
        pending_.emplace(event, action);
    }
}
void Runtime::handle(const transport::LinkStateChangeEvent& event,
                     sim::SimulationContext& context) {
    const auto found = pending_.find(context.current_event_id());
    if (found == pending_.end()) {
        throw std::logic_error{"unregistered scripted failure action"};
    }
    const auto action = found->second;
    auto& incident = report_.incidents.at(action.incident);
    const auto expected =
        action.recovery ? topology::OperationalState::Up : topology::OperationalState::Down;
    if (event.link != incident.specification.target || event.state != expected ||
        context.now() != action.timestamp) {
        throw std::logic_error{"scripted failure action identity mismatch"};
    }
    if (action.recovery) {
        transition(action.incident, telemetry::FailureTransition::RecoveryStarted, context.now());
        transport_->handle_link_state_change(event, context);
        active_.erase(event.link);
        incident.recovered = true;
        transition(action.incident, telemetry::FailureTransition::Recovered, context.now());
    } else {
        active_.emplace(event.link, action.incident);
        incident.applied = true;
        transport_->handle_link_state_change(event, context);
        transition(action.incident, telemetry::FailureTransition::Applied, context.now());
        transition(action.incident, telemetry::FailureTransition::Detected, context.now());
    }
    pending_.erase(found);
}
void Runtime::on_down_drop(const transport::DownDrop& drop) {
    const auto found = active_.find(drop.link.link);
    if (found == active_.end()) {
        return;
    }
    if (attributed_.size() >= limits_.attributed_chunks) {
        throw std::length_error{"failure attributed-chunk limit exceeded"};
    }
    if (!attributed_.insert(drop.chunk).second) {
        throw std::logic_error{"chunk attributed to a failure twice"};
    }
    auto& incident = report_.incidents.at(found->second);
    std::optional<workload::JobId> job;
    if (ring_ != nullptr && jobs_ != nullptr) {
        const auto collective = ring_->owner(drop.transfer);
        if (collective.has_value()) {
            job = jobs_->owner(*collective);
        }
        if (job.has_value()) {
            const auto snapshot = jobs_->snapshot(*job, drop.timestamp);
            if (!snapshot.has_value() || workload::terminal(snapshot->state)) {
                job.reset();
            }
        }
    }
    if (job.has_value() && !affected_.contains({incident.id, *job})) {
        if (affected_.size() >= limits_.affected_jobs) {
            throw std::length_error{"failure affected-job limit exceeded"};
        }
        incident.affected_jobs.push_back(*job);
        affected_.emplace(incident.id, *job);
    }
    incident.dropped = transport::add_traffic(incident.dropped, {drop.bytes.value(), 1});
    report_.drops.push_back({incident.id, drop, job});
}
Report Runtime::report() const {
    Report result = report_;
    for (auto& incident : result.incidents) {
        incident.effective_operational = operational(incident.specification.target);
    }
    return result;
}
} // namespace nexuslab::failures
