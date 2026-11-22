// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/topology/export.hpp"
#include "nexuslab/workload/engine.hpp"
#include <algorithm>
#include <limits>
#include <tuple>
#include <type_traits>

namespace nexuslab::failures {
std::uint64_t target_value(const Target& target) {
    return std::visit([](auto id) { return id.value(); }, target);
}
std::string_view kind_name(Kind kind) {
    switch (kind) {
    case Kind::LinkDown:
        return "link_down";
    case Kind::LinkDegraded:
        return "link_degraded";
    case Kind::PortDown:
        return "port_down";
    case Kind::SwitchDown:
        return "switch_down";
    case Kind::GpuDown:
        return "gpu_down";
    }
    throw std::invalid_argument{"unknown failure kind"};
}
namespace {
std::pair<unsigned, std::uint64_t> resource_key(const Incident& incident) {
    return {static_cast<unsigned>(incident.target.index()) + 1U, target_value(incident.target)};
}
void validate_port_target(const Incident& incident, const topology::TopologyGraph& graph) {
    const auto* id = std::get_if<topology::PortId>(&incident.target);
    const auto* port = id == nullptr ? nullptr : graph.find(*id);
    if (port == nullptr || port->role == topology::PortRole::Local ||
        port->state != topology::OperationalState::Up) {
        throw std::invalid_argument{"failure target must be an initially up fabric port"};
    }
    if (port->owner.kind() == topology::NodeKind::Switch &&
        graph.find(topology::SwitchId{port->owner.value()})->state !=
            topology::OperationalState::Up) {
        throw std::invalid_argument{"failure port owner is initially down"};
    }
}
void validate_target(const Incident& incident, const topology::TopologyGraph& graph) {
    if (incident.kind == Kind::LinkDown || incident.kind == Kind::LinkDegraded) {
        const auto* id = std::get_if<topology::LinkId>(&incident.target);
        const auto* link = id == nullptr ? nullptr : graph.find(*id);
        if (link == nullptr || link->kind != topology::LinkKind::Fabric) {
            throw std::invalid_argument{"failure target must be a known fabric link"};
        }
        for (const auto& arc : topology::directed_links(*link)) {
            if (!graph.is_operational(arc)) {
                throw std::invalid_argument{"failure target is initially blocked"};
            }
        }
        return;
    }
    if (incident.kind == Kind::PortDown) {
        validate_port_target(incident, graph);
        return;
    }
    if (incident.kind == Kind::SwitchDown) {
        const auto* id = std::get_if<topology::SwitchId>(&incident.target);
        const auto* sw = id == nullptr ? nullptr : graph.find(*id);
        if (sw == nullptr || sw->state != topology::OperationalState::Up) {
            throw std::invalid_argument{"failure target must be an initially up switch"};
        }
        return;
    }
    const auto* id = std::get_if<topology::GpuId>(&incident.target);
    if (id == nullptr || graph.find(*id) == nullptr ||
        id->value() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument{"failure target must be a known GPU"};
    }
}
} // namespace
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
    static_cast<void>(traffic::validate(script.loads, graph));
    std::vector<std::size_t> order;
    std::vector<Action> actions;
    for (std::size_t i = 0; i < script.incidents.size(); ++i) {
        const auto& incident = script.incidents[i];
        static_cast<void>(kind_name(incident.kind));
        validate_target(incident, graph);
        if ((incident.kind == Kind::LinkDegraded) != incident.bandwidth.has_value()) {
            throw std::invalid_argument{"only degraded links require bandwidth_bps"};
        }
        if (incident.bandwidth.has_value() && incident.bandwidth->value() == 0) {
            throw std::invalid_argument{"degraded capacity must be positive"};
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
        return std::tuple{resource_key(script.incidents[a]), script.incidents[a].onset} <
               std::tuple{resource_key(script.incidents[b]), script.incidents[b].onset};
    });
    for (std::size_t i = 1; i < order.size(); ++i) {
        const auto& previous = script.incidents[order[i - 1]];
        const auto& next = script.incidents[order[i]];
        if (resource_key(previous) == resource_key(next) &&
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
        integer(static_cast<std::uint64_t>(incident.kind));
        integer(target_value(incident.target));
        integer(incident.onset.count());
        integer(incident.recovery.has_value() ? 1 : 0);
        if (incident.recovery.has_value()) {
            integer(incident.recovery->count());
        }
        integer(incident.bandwidth.has_value() ? 1 : 0);
        if (incident.bandwidth.has_value()) {
            integer(incident.bandwidth->value());
        }
    }
    integer(script.loads.size());
    for (std::size_t i = 0; i < script.loads.size(); ++i) {
        const auto& load = script.loads[i];
        integer(i + 1);
        integer(static_cast<std::uint64_t>(load.kind));
        integer(load.source.value());
        integer(load.destination.value());
        integer(load.onset.count());
        integer(load.count);
        integer(load.interval.count());
        integer(load.bytes.value());
        integer(load.chunk_bytes.value());
    }
    return hash;
}
Runtime::Runtime(const Script& script, topology::TopologyGraph& graph,
                 transport::TransportRuntime& transport, const collective::RingExecutor* ring,
                 workload::WorkloadEngine* jobs, Limits limits)
    : graph_{&graph}, transport_{&transport}, ring_{ring}, jobs_{jobs}, limits_{limits},
      report_{script_digest(script, graph), {}, {}, {}},
      calendar_{validate(script, graph, limits)} {
    for (std::size_t i = 0; i < script.incidents.size(); ++i) {
        const auto& incident = script.incidents[i];
        if (incident.kind == Kind::LinkDown || incident.kind == Kind::LinkDegraded) {
            const auto id = std::get<topology::LinkId>(incident.target);
            const auto* a = transport.find_service({id, topology::LinkDirection::AToB});
            const auto* b = transport.find_service({id, topology::LinkDirection::BToA});
            if (a == nullptr || b == nullptr) {
                throw std::invalid_argument{"failure target requires both configured directions"};
            }
            baseline_.insert_or_assign(id, std::array{a->queue().configuration().bandwidth,
                                                      b->queue().configuration().bandwidth});
            if (incident.bandwidth.has_value() &&
                (*incident.bandwidth >= a->queue().configuration().bandwidth ||
                 *incident.bandwidth >= b->queue().configuration().bandwidth)) {
                throw std::invalid_argument{"degraded capacity must be below both baselines"};
            }
        }
        if (incident.kind == Kind::GpuDown &&
            (jobs_ == nullptr ||
             !jobs_->resource(std::get<topology::GpuId>(incident.target)).healthy)) {
            throw std::invalid_argument{"GPU failure requires scheduler and initially healthy GPU"};
        }
        if (incident.kind != Kind::LinkDown) {
            report_.outcome_version = 3;
        }
        report_.incidents.push_back(
            {telemetry::FailureId{i + 1}, incident, false, false, true, {}, {}});
    }
    // Reserve only after validating every incident, before any event or transfer exists.
    for (const auto& incident : script.incidents) {
        if (incident.bandwidth.has_value()) {
            transport.reserve_capacity(std::get<topology::LinkId>(incident.target),
                                       *incident.bandwidth);
        }
    }
    if (!script.loads.empty()) {
        report_.outcome_version = 4;
    }
    transport_->set_down_drop_observer(this);
}
Runtime::~Runtime() { transport_->set_down_drop_observer(nullptr); }
Runtime::ResourceKey Runtime::key(const Incident& incident) { return resource_key(incident); }
bool Runtime::operational(const Incident& incident) const {
    return std::visit(
        [this](auto id) {
            using Id = decltype(id);
            if constexpr (std::is_same_v<Id, topology::LinkId>) {
                const auto arcs = topology::directed_links(*graph_->find(id));
                return graph_->is_operational(arcs[0]) && graph_->is_operational(arcs[1]);
            } else if constexpr (std::is_same_v<Id, topology::PortId>) {
                const auto* port = graph_->find(id);
                const bool owner_up =
                    port->owner.kind() != topology::NodeKind::Switch ||
                    graph_->find(topology::SwitchId{port->owner.value()})->state ==
                        topology::OperationalState::Up;
                return port->state == topology::OperationalState::Up && owner_up;
            } else if constexpr (std::is_same_v<Id, topology::SwitchId>) {
                return graph_->find(id)->state == topology::OperationalState::Up;
            } else {
                return jobs_->resource(id).healthy;
            }
        },
        incident.target);
}
void Runtime::transition(std::size_t index, telemetry::FailureTransition kind, sim::SimTimeNs now) {
    auto& incident = report_.incidents.at(index);
    incident.effective_operational = operational(incident.specification);
    incident.capacity_revision = transport_->capacity_revision();
    report_.timeline.push_back(
        {incident.id, kind, now, incident.effective_operational, incident.capacity_revision});
}
sim::EventPayload Runtime::event(const Action& action) const {
    const auto& incident = report_.incidents.at(action.incident).specification;
    const auto state =
        action.recovery ? topology::OperationalState::Up : topology::OperationalState::Down;
    switch (incident.kind) {
    case Kind::LinkDown:
        return transport::LinkStateChangeEvent{std::get<topology::LinkId>(incident.target), state};
    case Kind::LinkDegraded: {
        const auto link = std::get<topology::LinkId>(incident.target);
        const auto baseline = baseline_.at(link);
        if (action.recovery) {
            return transport::LinkCapacityChangeEvent{link, baseline[0], baseline[1]};
        }
        if (!incident.bandwidth.has_value()) {
            throw std::logic_error{"missing degraded capacity"};
        }
        return transport::LinkCapacityChangeEvent{link, *incident.bandwidth, *incident.bandwidth};
    }
    case Kind::PortDown:
        return transport::PortStateChangeEvent{std::get<topology::PortId>(incident.target), state};
    case Kind::SwitchDown:
        return transport::SwitchStateChangeEvent{std::get<topology::SwitchId>(incident.target),
                                                 state};
    case Kind::GpuDown:
        return workload::WorkloadEvent{
            workload::JobId{0},
            action.recovery ? workload::WorkloadEventKind::GpuUp
                            : workload::WorkloadEventKind::GpuDown,
            static_cast<std::uint32_t>(std::get<topology::GpuId>(incident.target).value())};
    }
    throw std::logic_error{"unknown incident kind"};
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
        pending_.emplace(
            simulation.schedule({action.timestamp, sim::EventPriority::Critical, event(action)}),
            action);
    }
}
bool Runtime::owns(sim::EventId event_id) const { return pending_.contains(event_id); }
void Runtime::apply(const sim::EventPayload& payload, sim::SimulationContext& context) {
    std::visit(
        [this, &context](const auto& value) {
            using E = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<E, transport::LinkStateChangeEvent>) {
                transport_->handle_link_state_change(value, context);
            } else if constexpr (std::is_same_v<E, transport::PortStateChangeEvent>) {
                transport_->handle_port_state_change(value, context);
            } else if constexpr (std::is_same_v<E, transport::SwitchStateChangeEvent>) {
                transport_->handle_switch_state_change(value, context);
            } else if constexpr (std::is_same_v<E, transport::LinkCapacityChangeEvent>) {
                transport_->handle_capacity_change(value, context);
            } else if constexpr (std::is_same_v<E, workload::WorkloadEvent>) {
                jobs_->handle(value, context);
            } else {
                throw std::logic_error{"unexpected failure action payload"};
            }
        },
        payload);
}
void Runtime::process(const sim::EventPayload& payload, sim::SimulationContext& context) {
    const auto found = pending_.find(context.current_event_id());
    if (found == pending_.end()) {
        throw std::logic_error{"unregistered scripted failure action"};
    }
    const auto action = found->second;
    auto& incident = report_.incidents.at(action.incident);
    if (payload != event(action) || context.now() != action.timestamp) {
        throw std::logic_error{"scripted failure action identity mismatch"};
    }
    if (action.recovery) {
        transition(action.incident, telemetry::FailureTransition::RecoveryStarted, context.now());
        apply(payload, context);
        active_.erase(key(incident.specification));
        incident.recovered = true;
        transition(action.incident, telemetry::FailureTransition::Recovered, context.now());
    } else {
        active_.emplace(key(incident.specification), action.incident);
        incident.applied = true;
        if (incident.specification.kind == Kind::GpuDown) {
            const auto owner =
                jobs_->resource(std::get<topology::GpuId>(incident.specification.target)).owner;
            if (owner.has_value()) {
                affect_job(action.incident, *owner);
            }
        }
        apply(payload, context);
        transition(action.incident, telemetry::FailureTransition::Applied, context.now());
        transition(action.incident, telemetry::FailureTransition::Detected, context.now());
    }
    pending_.erase(found);
}
void Runtime::handle(const transport::LinkStateChangeEvent& event_value,
                     sim::SimulationContext& context) {
    process(event_value, context);
}
void Runtime::handle(const transport::PortStateChangeEvent& event_value,
                     sim::SimulationContext& context) {
    process(event_value, context);
}
void Runtime::handle(const transport::SwitchStateChangeEvent& event_value,
                     sim::SimulationContext& context) {
    process(event_value, context);
}
void Runtime::handle(const transport::LinkCapacityChangeEvent& event_value,
                     sim::SimulationContext& context) {
    process(event_value, context);
}
void Runtime::handle(const workload::WorkloadEvent& event_value, sim::SimulationContext& context) {
    process(event_value, context);
}
void Runtime::affect_job(std::size_t index, workload::JobId job) {
    auto& incident = report_.incidents.at(index);
    if (affected_.contains({incident.id, job})) {
        return;
    }
    if (affected_.size() >= limits_.affected_jobs) {
        throw std::length_error{"failure affected-job limit exceeded"};
    }
    incident.affected_jobs.push_back(job);
    affected_.emplace(incident.id, job);
}
std::size_t Runtime::blocking_incident(const topology::PhysicalLink& link) const {
    std::vector<ResourceKey> keys{
        {1, link.id.value()}, {2, link.endpoint_a.value()}, {2, link.endpoint_b.value()}};
    for (const auto port : {link.endpoint_a, link.endpoint_b}) {
        const auto owner = graph_->find(port)->owner;
        if (owner.kind() == topology::NodeKind::Switch) {
            keys.emplace_back(3, owner.value());
        }
    }
    const auto not_found = report_.incidents.size();
    std::size_t selected = not_found;
    for (const auto& candidate : keys) {
        const auto found = active_.find(candidate);
        if (found != active_.end() &&
            report_.incidents[found->second].specification.kind != Kind::LinkDegraded) {
            selected = std::min(selected, found->second);
        }
    }
    return selected;
}
void Runtime::on_down_drop(const transport::DownDrop& drop) {
    const auto* link = graph_->find(drop.link.link);
    const auto not_found = report_.incidents.size();
    const auto selected = blocking_incident(*link);
    if (selected == not_found) {
        return;
    }
    if (attributed_.size() >= limits_.attributed_chunks) {
        throw std::length_error{"failure attributed-chunk limit exceeded"};
    }
    if (!attributed_.insert(drop.chunk).second) {
        throw std::logic_error{"chunk attributed to a failure twice"};
    }
    auto& incident = report_.incidents.at(selected);
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
    if (job.has_value()) {
        affect_job(selected, *job);
    }
    incident.dropped = transport::add_traffic(incident.dropped, {drop.bytes.value(), 1});
    report_.drops.push_back({incident.id, drop, job});
}
Report Runtime::report() const {
    Report result = report_;
    result.capacity_revision = transport_->capacity_revision();
    for (auto& incident : result.incidents) {
        incident.effective_operational = operational(incident.specification);
        incident.capacity_revision = result.capacity_revision;
    }
    return result;
}
} // namespace nexuslab::failures
