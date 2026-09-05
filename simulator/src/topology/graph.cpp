// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/graph.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nexuslab::topology {
namespace {

template <typename Entity, typename Id>
[[nodiscard]] const Entity* find_dense(const std::vector<Entity>& entities, Id id) noexcept {
    if (id.value() > std::numeric_limits<std::size_t>::max()) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(id.value());
    if (index >= entities.size() || entities[index].id != id) {
        return nullptr;
    }
    return &entities[index];
}

template <typename Entity, typename Id>
[[nodiscard]] Entity* find_dense(std::vector<Entity>& entities, Id id) noexcept {
    if (id.value() > std::numeric_limits<std::size_t>::max()) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(id.value());
    if (index >= entities.size() || entities[index].id != id) {
        return nullptr;
    }
    return &entities[index];
}

struct PathMetrics final {
    std::size_t hops{std::numeric_limits<std::size_t>::max()};
    std::uint64_t path_count{0};
};

class ShortestPathState final {
  public:
    explicit ShortestPathState(const TopologyGraph& graph)
        : gpus_(graph.gpus().size()), nics_(graph.nics().size()),
          switches_(graph.switches().size()) {}

    [[nodiscard]] PathMetrics& metrics(NodeId node) {
        return metrics_for(node)[static_cast<std::size_t>(node.value())];
    }

    [[nodiscard]] const PathMetrics& metrics(NodeId node) const {
        return metrics_for(node)[static_cast<std::size_t>(node.value())];
    }

  private:
    [[nodiscard]] std::vector<PathMetrics>& metrics_for(NodeId node) {
        switch (node.kind()) {
        case NodeKind::Gpu:
            return gpus_;
        case NodeKind::Nic:
            return nics_;
        case NodeKind::Switch:
            return switches_;
        }
        throw std::logic_error{"unknown topology node kind"};
    }

    [[nodiscard]] const std::vector<PathMetrics>& metrics_for(NodeId node) const {
        switch (node.kind()) {
        case NodeKind::Gpu:
            return gpus_;
        case NodeKind::Nic:
            return nics_;
        case NodeKind::Switch:
            return switches_;
        }
        throw std::logic_error{"unknown topology node kind"};
    }

    std::vector<PathMetrics> gpus_;
    std::vector<PathMetrics> nics_;
    std::vector<PathMetrics> switches_;
};

} // namespace

RackId TopologyGraph::add_rack() {
    const RackId id = rack_ids_.next();
    racks_.push_back(Rack{id, {}, {}, {}});
    return id;
}

NicId TopologyGraph::add_nic(RackId rack) {
    const Rack* rack_entity = find(rack);
    if (rack_entity == nullptr) {
        throw std::invalid_argument{"cannot add NIC to an unknown rack"};
    }

    const NicId id = nic_ids_.next();
    nics_.push_back(Nic{id, rack, {}, {}});
    nic_adjacency_.emplace_back();
    racks_[static_cast<std::size_t>(rack.value())].nics.push_back(id);
    return id;
}

GpuId TopologyGraph::add_gpu(NicId attached_nic) {
    const Nic* nic_entity = find(attached_nic);
    if (nic_entity == nullptr) {
        throw std::invalid_argument{"cannot add GPU to an unknown NIC"};
    }

    const GpuId gpu_id = gpu_ids_.next();
    const PortId gpu_port = port_ids_.next();
    const PortId nic_port = port_ids_.next();
    const LinkId link_id = link_ids_.next();
    const RackId rack = nic_entity->rack;
    const NodeId gpu_node{gpu_id};
    const NodeId nic_node{attached_nic};

    gpus_.push_back(GpuWorker{gpu_id, rack, attached_nic, gpu_port});
    gpu_adjacency_.emplace_back();
    nics_[static_cast<std::size_t>(attached_nic.value())].attached_gpus.push_back(gpu_id);
    nics_[static_cast<std::size_t>(attached_nic.value())].ports.push_back(nic_port);
    racks_[static_cast<std::size_t>(rack.value())].gpus.push_back(gpu_id);
    ports_.push_back(Port{gpu_port, gpu_node, PortRole::Local, OperationalState::Up});
    ports_.push_back(Port{nic_port, nic_node, PortRole::Local, OperationalState::Up});
    append_link(
        PhysicalLink{link_id, gpu_port, nic_port, LinkKind::LocalAttachment, OperationalState::Up},
        gpu_node, nic_node);
    return gpu_id;
}

SwitchId TopologyGraph::add_leaf_switch(RackId rack) {
    const Rack* rack_entity = find(rack);
    if (rack_entity == nullptr) {
        throw std::invalid_argument{"cannot add leaf switch to an unknown rack"};
    }

    const SwitchId id = switch_ids_.next();
    switches_.push_back(Switch{id, rack, SwitchRole::Leaf, {}, OperationalState::Up});
    switch_adjacency_.emplace_back();
    racks_[static_cast<std::size_t>(rack.value())].leaf_switches.push_back(id);
    return id;
}

SwitchId TopologyGraph::add_spine_switch() {
    const SwitchId id = switch_ids_.next();
    switches_.push_back(Switch{id, std::nullopt, SwitchRole::Spine, {}, OperationalState::Up});
    switch_adjacency_.emplace_back();
    return id;
}

LinkId TopologyGraph::connect_fabric(NodeId endpoint_a, PortRole role_a, NodeId endpoint_b,
                                     PortRole role_b) {
    if (!contains(endpoint_a) || !contains(endpoint_b)) {
        throw std::invalid_argument{"cannot connect an unknown topology node"};
    }
    if (endpoint_a == endpoint_b) {
        throw std::invalid_argument{"cannot connect a topology node to itself"};
    }
    if (endpoint_a.kind() == NodeKind::Gpu || endpoint_b.kind() == NodeKind::Gpu) {
        throw std::invalid_argument{"GPU fabric connections must use the attached NIC"};
    }
    if (role_a == PortRole::Local || role_b == PortRole::Local) {
        throw std::invalid_argument{"fabric connections require fabric port roles"};
    }
    if (directly_connected(endpoint_a, endpoint_b)) {
        throw std::invalid_argument{"parallel physical links are not supported"};
    }

    const PortId port_a = port_ids_.next();
    const PortId port_b = port_ids_.next();
    const LinkId link_id = link_ids_.next();
    append_owner_port(endpoint_a, port_a);
    append_owner_port(endpoint_b, port_b);
    ports_.push_back(Port{port_a, endpoint_a, role_a, OperationalState::Up});
    ports_.push_back(Port{port_b, endpoint_b, role_b, OperationalState::Up});
    append_link(PhysicalLink{link_id, port_a, port_b, LinkKind::Fabric, OperationalState::Up},
                endpoint_a, endpoint_b);
    return link_id;
}

std::uint64_t TopologyGraph::operational_revision() const noexcept { return operational_revision_; }

bool TopologyGraph::set_link_state(LinkId id, OperationalState state) noexcept {
    PhysicalLink* link = find_dense(links_, id);
    if (link == nullptr) {
        return false;
    }
    if (state != OperationalState::Up && state != OperationalState::Down) {
        return false;
    }
    if (link->state != state) {
        if (operational_revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        ++operational_revision_;
        link->state = state;
    }
    return true;
}

bool TopologyGraph::set_port_state(PortId id, OperationalState state) noexcept {
    Port* port = find_dense(ports_, id);
    if (port == nullptr) {
        return false;
    }
    if (state != OperationalState::Up && state != OperationalState::Down) {
        return false;
    }
    if (port->state != state) {
        if (operational_revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        ++operational_revision_;
        port->state = state;
    }
    return true;
}

bool TopologyGraph::set_switch_state(SwitchId id, OperationalState state) noexcept {
    Switch* switch_entity = find_dense(switches_, id);
    if (switch_entity == nullptr) {
        return false;
    }
    if (state != OperationalState::Up && state != OperationalState::Down) {
        return false;
    }
    if (switch_entity->state != state) {
        if (operational_revision_ == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        ++operational_revision_;
        switch_entity->state = state;
    }
    return true;
}

const GpuWorker* TopologyGraph::find(GpuId id) const noexcept { return find_dense(gpus_, id); }

const Nic* TopologyGraph::find(NicId id) const noexcept { return find_dense(nics_, id); }

const Switch* TopologyGraph::find(SwitchId id) const noexcept { return find_dense(switches_, id); }

const Rack* TopologyGraph::find(RackId id) const noexcept { return find_dense(racks_, id); }

const Port* TopologyGraph::find(PortId id) const noexcept { return find_dense(ports_, id); }

const PhysicalLink* TopologyGraph::find(LinkId id) const noexcept { return find_dense(links_, id); }

std::span<const GpuWorker> TopologyGraph::gpus() const noexcept { return gpus_; }

std::span<const Nic> TopologyGraph::nics() const noexcept { return nics_; }

std::span<const Switch> TopologyGraph::switches() const noexcept { return switches_; }

std::span<const Rack> TopologyGraph::racks() const noexcept { return racks_; }

std::span<const Port> TopologyGraph::ports() const noexcept { return ports_; }

std::span<const PhysicalLink> TopologyGraph::links() const noexcept { return links_; }

std::span<const DirectedLink> TopologyGraph::outgoing(NodeId node) const { return adjacency(node); }

bool TopologyGraph::is_operational(const DirectedLink& directed_link) const noexcept {
    const PhysicalLink* link = find(directed_link.id.link);
    const Port* source = find(directed_link.source);
    const Port* destination = find(directed_link.destination);
    if (link == nullptr || source == nullptr || destination == nullptr) {
        return false;
    }
    const auto directions = directed_links(*link);
    const bool matches_physical_link =
        std::ranges::find(directions, directed_link) != directions.end();
    return matches_physical_link && link->state == OperationalState::Up &&
           source->state == OperationalState::Up && destination->state == OperationalState::Up &&
           node_is_operational(source->owner) && node_is_operational(destination->owner);
}

std::optional<ShortestPathSummary> TopologyGraph::shortest_path_summary(NodeId source,
                                                                        NodeId destination) const {
    return shortest_path_summary_impl(source, destination, true);
}

std::optional<ShortestPathSummary>
TopologyGraph::shortest_path_summary_impl(NodeId source, NodeId destination,
                                          bool count_equal_paths) const {
    if (!contains(source) || !contains(destination)) {
        throw std::invalid_argument{"cannot find a path involving an unknown topology node"};
    }
    if (!node_is_operational(source) || !node_is_operational(destination)) {
        return std::nullopt;
    }

    ShortestPathState state{*this};
    std::queue<NodeId> pending;
    state.metrics(source) = PathMetrics{0U, 1U};
    pending.push(source);

    while (!pending.empty()) {
        const NodeId node = pending.front();
        pending.pop();
        const PathMetrics current = state.metrics(node);

        for (const DirectedLink& directed_link : outgoing(node)) {
            if (!is_operational(directed_link)) {
                continue;
            }
            const Port* destination_port = find(directed_link.destination);
            if (destination_port == nullptr) {
                continue;
            }
            PathMetrics& next = state.metrics(destination_port->owner);
            const std::size_t next_hops = current.hops + 1U;
            if (next.hops == std::numeric_limits<std::size_t>::max()) {
                next = PathMetrics{next_hops, current.path_count};
                pending.push(destination_port->owner);
            } else if (count_equal_paths && next.hops == next_hops) {
                if (std::numeric_limits<std::uint64_t>::max() - next.path_count <
                    current.path_count) {
                    throw std::overflow_error{"equal-cost shortest-path count overflow"};
                }
                next.path_count += current.path_count;
            }
        }
    }

    const PathMetrics result = state.metrics(destination);
    if (result.hops == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return ShortestPathSummary{result.hops, result.path_count};
}

std::optional<std::size_t> TopologyGraph::shortest_hops(NodeId source, NodeId destination) const {
    const auto summary = shortest_path_summary_impl(source, destination, false);
    if (!summary.has_value()) {
        return std::nullopt;
    }
    return summary->hops;
}

bool TopologyGraph::reachable(NodeId source, NodeId destination) const {
    return shortest_hops(source, destination).has_value();
}

bool TopologyGraph::contains(NodeId node) const noexcept {
    switch (node.kind()) {
    case NodeKind::Gpu:
        return find(GpuId{node.value()}) != nullptr;
    case NodeKind::Nic:
        return find(NicId{node.value()}) != nullptr;
    case NodeKind::Switch:
        return find(SwitchId{node.value()}) != nullptr;
    }
    return false;
}

bool TopologyGraph::node_is_operational(NodeId node) const noexcept {
    if (!contains(node)) {
        return false;
    }
    if (node.kind() != NodeKind::Switch) {
        return true;
    }
    const Switch* switch_entity = find(SwitchId{node.value()});
    return switch_entity != nullptr && switch_entity->state == OperationalState::Up;
}

bool TopologyGraph::directly_connected(NodeId endpoint_a, NodeId endpoint_b) const {
    return std::ranges::any_of(adjacency(endpoint_a), [&](const DirectedLink& directed_link) {
        const Port* destination = find(directed_link.destination);
        if (destination == nullptr) {
            throw std::logic_error{"topology adjacency references an unknown port"};
        }
        return destination->owner == endpoint_b;
    });
}

std::vector<DirectedLink>& TopologyGraph::mutable_adjacency(NodeId node) {
    const auto index = static_cast<std::size_t>(node.value());
    switch (node.kind()) {
    case NodeKind::Gpu:
        return gpu_adjacency_.at(index);
    case NodeKind::Nic:
        return nic_adjacency_.at(index);
    case NodeKind::Switch:
        return switch_adjacency_.at(index);
    }
    throw std::logic_error{"unknown topology node kind"};
}

const std::vector<DirectedLink>& TopologyGraph::adjacency(NodeId node) const {
    if (!contains(node)) {
        throw std::out_of_range{"cannot query adjacency for an unknown topology node"};
    }

    const auto index = static_cast<std::size_t>(node.value());
    switch (node.kind()) {
    case NodeKind::Gpu:
        return gpu_adjacency_[index];
    case NodeKind::Nic:
        return nic_adjacency_[index];
    case NodeKind::Switch:
        return switch_adjacency_[index];
    }
    throw std::logic_error{"unknown topology node kind"};
}

void TopologyGraph::append_owner_port(NodeId owner, PortId port) {
    const auto index = static_cast<std::size_t>(owner.value());
    switch (owner.kind()) {
    case NodeKind::Gpu:
        throw std::logic_error{"GPU workers have exactly one local port"};
    case NodeKind::Nic:
        nics_.at(index).ports.push_back(port);
        return;
    case NodeKind::Switch:
        switches_.at(index).ports.push_back(port);
        return;
    }
    throw std::logic_error{"unknown topology node kind"};
}

void TopologyGraph::append_link(PhysicalLink link, NodeId endpoint_a, NodeId endpoint_b) {
    const auto directions = directed_links(link);
    links_.push_back(link);
    mutable_adjacency(endpoint_a).push_back(directions[0]);
    mutable_adjacency(endpoint_b).push_back(directions[1]);
}

} // namespace nexuslab::topology
