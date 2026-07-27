// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/validation.hpp"

#include "nexuslab/topology/clos.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <vector>

namespace nexuslab::topology {
namespace {

template <typename Range, typename Value>
[[nodiscard]] std::size_t count_value(const Range& range, const Value& value) {
    return static_cast<std::size_t>(std::ranges::count(range, value));
}

template <typename Entity> [[nodiscard]] bool has_dense_ids(std::span<const Entity> entities) {
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (entities[index].id.value() != index) {
            return false;
        }
    }
    return true;
}

void count_port_endpoint(std::vector<std::uint8_t>& counts, PortId endpoint) {
    if (endpoint.value() > std::numeric_limits<std::size_t>::max()) {
        return;
    }
    const auto index = static_cast<std::size_t>(endpoint.value());
    if (index < counts.size() && counts[index] < 2U) {
        ++counts[index];
    }
}

[[nodiscard]] std::vector<std::uint8_t> port_link_counts(const TopologyGraph& graph) {
    std::vector<std::uint8_t> counts(graph.ports().size(), 0U);
    for (const PhysicalLink& link : graph.links()) {
        count_port_endpoint(counts, link.endpoint_a);
        count_port_endpoint(counts, link.endpoint_b);
    }
    return counts;
}

[[nodiscard]] bool node_exists(const TopologyGraph& graph, NodeId node) {
    switch (node.kind()) {
    case NodeKind::Gpu:
        return graph.find(GpuId{node.value()}) != nullptr;
    case NodeKind::Nic:
        return graph.find(NicId{node.value()}) != nullptr;
    case NodeKind::Switch:
        return graph.find(SwitchId{node.value()}) != nullptr;
    }
    return false;
}

class StructuralVisited final {
  public:
    explicit StructuralVisited(const TopologyGraph& graph)
        : gpus_(graph.gpus().size()), nics_(graph.nics().size()),
          switches_(graph.switches().size()) {}

    [[nodiscard]] bool mark(NodeId node) {
        auto& visited = values(node.kind());
        const auto index = static_cast<std::size_t>(node.value());
        if (index >= visited.size() || visited[index] != 0U) {
            return false;
        }
        visited[index] = 1U;
        return true;
    }

    [[nodiscard]] bool contains(NodeId node) const {
        const auto& visited = values(node.kind());
        const auto index = static_cast<std::size_t>(node.value());
        return index < visited.size() && visited[index] != 0U;
    }

  private:
    [[nodiscard]] std::vector<std::uint8_t>& values(NodeKind kind) {
        return const_cast<std::vector<std::uint8_t>&>(
            static_cast<const StructuralVisited&>(*this).values(kind));
    }

    [[nodiscard]] const std::vector<std::uint8_t>& values(NodeKind kind) const {
        switch (kind) {
        case NodeKind::Gpu:
            return gpus_;
        case NodeKind::Nic:
            return nics_;
        case NodeKind::Switch:
            return switches_;
        }
        return gpus_;
    }

    std::vector<std::uint8_t> gpus_;
    std::vector<std::uint8_t> nics_;
    std::vector<std::uint8_t> switches_;
};

class TopologyValidator final {
  public:
    explicit TopologyValidator(const TopologyGraph& graph)
        : graph_{graph}, port_link_counts_{port_link_counts(graph)} {}

    [[nodiscard]] ValidationReport validate() {
        validate_identity();
        validate_racks();
        validate_gpus();
        validate_nics();
        validate_switches();
        validate_ports();
        validate_links();
        validate_connectivity();
        return std::move(report_);
    }

  private:
    void add(ValidationErrorCode code, std::string message,
             std::optional<NodeId> node = std::nullopt, std::optional<LinkId> link = std::nullopt) {
        report_.errors.push_back(ValidationError{code, node, link, std::move(message)});
    }

    void validate_identity() {
        if (graph_.gpus().empty()) {
            add(ValidationErrorCode::EmptyTopology, "topology must contain at least one GPU");
        }
        validate_dense(graph_.gpus(), "GPU");
        validate_dense(graph_.nics(), "NIC");
        validate_dense(graph_.switches(), "switch");
        validate_dense(graph_.racks(), "rack");
        validate_dense(graph_.ports(), "port");
        validate_dense(graph_.links(), "link");
    }

    template <typename Entity>
    void validate_dense(std::span<const Entity> entities, const char* entity_kind) {
        if (!has_dense_ids(entities)) {
            add(ValidationErrorCode::NonDenseId,
                std::string{entity_kind} + " identifiers must be dense and index-aligned");
        }
    }

    void validate_racks() {
        for (const Rack& rack : graph_.racks()) {
            for (GpuId gpu_id : rack.gpus) {
                const GpuWorker* gpu = graph_.find(gpu_id);
                if (gpu == nullptr) {
                    add(ValidationErrorCode::UnknownReference, "rack references an unknown GPU");
                } else if (gpu->rack != rack.id || count_value(rack.gpus, gpu_id) != 1U) {
                    add(ValidationErrorCode::RelationshipMismatch,
                        "rack and GPU membership must be reciprocal", NodeId{gpu_id});
                }
            }
            validate_rack_nics(rack);
            validate_rack_switches(rack);
        }
    }

    void validate_rack_nics(const Rack& rack) {
        for (NicId nic_id : rack.nics) {
            const Nic* nic = graph_.find(nic_id);
            if (nic == nullptr) {
                add(ValidationErrorCode::UnknownReference, "rack references an unknown NIC");
            } else if (nic->rack != rack.id || count_value(rack.nics, nic_id) != 1U) {
                add(ValidationErrorCode::RelationshipMismatch,
                    "rack and NIC membership must be reciprocal", NodeId{nic_id});
            }
        }
    }

    void validate_rack_switches(const Rack& rack) {
        for (SwitchId switch_id : rack.leaf_switches) {
            const Switch* switch_entity = graph_.find(switch_id);
            if (switch_entity == nullptr) {
                add(ValidationErrorCode::UnknownReference, "rack references an unknown switch");
            } else if (switch_entity->role != SwitchRole::Leaf || switch_entity->rack != rack.id ||
                       count_value(rack.leaf_switches, switch_id) != 1U) {
                add(ValidationErrorCode::RelationshipMismatch,
                    "rack and leaf-switch membership must be reciprocal", NodeId{switch_id});
            }
        }
    }

    void validate_gpus() {
        for (const GpuWorker& gpu : graph_.gpus()) {
            const Nic* nic = graph_.find(gpu.attached_nic);
            const Rack* rack = graph_.find(gpu.rack);
            const Port* port = graph_.find(gpu.port);
            if (nic == nullptr || rack == nullptr || port == nullptr) {
                add(ValidationErrorCode::UnknownReference,
                    "GPU references an unknown rack, NIC, or port", NodeId{gpu.id});
                continue;
            }
            if (nic->rack != gpu.rack || count_value(nic->attached_gpus, gpu.id) != 1U ||
                count_value(rack->gpus, gpu.id) != 1U) {
                add(ValidationErrorCode::RelationshipMismatch,
                    "GPU, NIC, and rack membership must be reciprocal", NodeId{gpu.id});
            }
            if (port->owner != NodeId{gpu.id} || port->role != PortRole::Local) {
                add(ValidationErrorCode::PortOwnershipMismatch,
                    "GPU local port has an invalid owner or role", NodeId{gpu.id});
            }
        }
    }

    void validate_nics() {
        for (const Nic& nic : graph_.nics()) {
            const Rack* rack = graph_.find(nic.rack);
            if (rack == nullptr) {
                add(ValidationErrorCode::UnknownReference, "NIC references an unknown rack",
                    NodeId{nic.id});
            } else if (count_value(rack->nics, nic.id) != 1U) {
                add(ValidationErrorCode::RelationshipMismatch,
                    "NIC and rack membership must be reciprocal", NodeId{nic.id});
            }
            for (GpuId gpu_id : nic.attached_gpus) {
                const GpuWorker* gpu = graph_.find(gpu_id);
                if (gpu == nullptr) {
                    add(ValidationErrorCode::UnknownReference, "NIC references an unknown GPU",
                        NodeId{nic.id});
                } else if (gpu->attached_nic != nic.id ||
                           count_value(nic.attached_gpus, gpu_id) != 1U) {
                    add(ValidationErrorCode::RelationshipMismatch,
                        "NIC and GPU attachment must be reciprocal", NodeId{nic.id});
                }
            }
            validate_owned_ports(NodeId{nic.id}, nic.ports);
        }
    }

    void validate_switches() {
        for (const Switch& switch_entity : graph_.switches()) {
            const NodeId node{switch_entity.id};
            if (switch_entity.role == SwitchRole::Leaf) {
                const Rack* rack =
                    switch_entity.rack.has_value() ? graph_.find(*switch_entity.rack) : nullptr;
                if (rack == nullptr) {
                    add(ValidationErrorCode::UnknownReference,
                        "leaf switch must reference a known rack", node);
                } else if (count_value(rack->leaf_switches, switch_entity.id) != 1U) {
                    add(ValidationErrorCode::RelationshipMismatch,
                        "leaf switch and rack membership must be reciprocal", node);
                }
            } else if (switch_entity.rack.has_value()) {
                add(ValidationErrorCode::RelationshipMismatch,
                    "spine switch must not belong to a rack", node);
            }
            validate_owned_ports(node, switch_entity.ports);
        }
    }

    void validate_owned_ports(NodeId owner, const std::vector<PortId>& port_ids) {
        for (PortId port_id : port_ids) {
            const Port* port = graph_.find(port_id);
            if (port == nullptr) {
                add(ValidationErrorCode::UnknownReference, "node references an unknown port",
                    owner);
            } else if (port->owner != owner || count_value(port_ids, port_id) != 1U) {
                add(ValidationErrorCode::PortOwnershipMismatch,
                    "port ownership must be reciprocal and unique", owner);
            }
        }
    }

    void validate_ports() {
        for (const Port& port : graph_.ports()) {
            if (!owner_references(port)) {
                add(ValidationErrorCode::PortOwnershipMismatch,
                    "port owner does not reference the port exactly once", port.owner);
            }
            const bool id_fits_index = port.id.value() <= std::numeric_limits<std::size_t>::max();
            const auto index = id_fits_index ? static_cast<std::size_t>(port.id.value())
                                             : port_link_counts_.size();
            if (index >= port_link_counts_.size() || port_link_counts_[index] != 1U) {
                add(ValidationErrorCode::PortLinkCountMismatch,
                    "every port must terminate exactly one physical link", port.owner);
            }
        }
    }

    [[nodiscard]] bool owner_references(const Port& port) const {
        switch (port.owner.kind()) {
        case NodeKind::Gpu: {
            const GpuWorker* gpu = graph_.find(GpuId{port.owner.value()});
            return gpu != nullptr && gpu->port == port.id;
        }
        case NodeKind::Nic: {
            const Nic* nic = graph_.find(NicId{port.owner.value()});
            return nic != nullptr && count_value(nic->ports, port.id) == 1U;
        }
        case NodeKind::Switch: {
            const Switch* switch_entity = graph_.find(SwitchId{port.owner.value()});
            return switch_entity != nullptr && count_value(switch_entity->ports, port.id) == 1U;
        }
        }
        return false;
    }

    void validate_links() {
        for (const PhysicalLink& link : graph_.links()) {
            const Port* endpoint_a = graph_.find(link.endpoint_a);
            const Port* endpoint_b = graph_.find(link.endpoint_b);
            if (endpoint_a == nullptr || endpoint_b == nullptr) {
                add(ValidationErrorCode::UnknownReference,
                    "physical link references an unknown endpoint", std::nullopt, link.id);
                continue;
            }
            if (!node_exists(graph_, endpoint_a->owner) ||
                !node_exists(graph_, endpoint_b->owner)) {
                add(ValidationErrorCode::UnknownReference,
                    "physical link endpoint has an unknown owner", std::nullopt, link.id);
                continue;
            }
            if (endpoint_a->id == endpoint_b->id || endpoint_a->owner == endpoint_b->owner) {
                add(ValidationErrorCode::LinkEndpointMismatch,
                    "physical link endpoints must be distinct nodes", std::nullopt, link.id);
            }
            validate_link_kind(link, *endpoint_a, *endpoint_b);
            validate_adjacency(link, *endpoint_a, *endpoint_b);
        }
    }

    void validate_link_kind(const PhysicalLink& link, const Port& endpoint_a,
                            const Port& endpoint_b) {
        const bool is_gpu_nic =
            (endpoint_a.owner.kind() == NodeKind::Gpu &&
             endpoint_b.owner.kind() == NodeKind::Nic) ||
            (endpoint_a.owner.kind() == NodeKind::Nic && endpoint_b.owner.kind() == NodeKind::Gpu);
        const bool valid_local = link.kind == LinkKind::LocalAttachment && is_gpu_nic &&
                                 endpoint_a.role == PortRole::Local &&
                                 endpoint_b.role == PortRole::Local;
        const bool valid_fabric =
            link.kind == LinkKind::Fabric && endpoint_a.owner.kind() != NodeKind::Gpu &&
            endpoint_b.owner.kind() != NodeKind::Gpu && endpoint_a.role != PortRole::Local &&
            endpoint_b.role != PortRole::Local;
        if (!valid_local && !valid_fabric) {
            add(ValidationErrorCode::LinkKindMismatch,
                "link kind, endpoint node kinds, and port roles are inconsistent", std::nullopt,
                link.id);
        }
    }

    void validate_adjacency(const PhysicalLink& link, const Port& endpoint_a,
                            const Port& endpoint_b) {
        const auto directions = directed_links(link);
        if (count_value(graph_.outgoing(endpoint_a.owner), directions[0]) != 1U ||
            count_value(graph_.outgoing(endpoint_b.owner), directions[1]) != 1U) {
            add(ValidationErrorCode::AdjacencyMismatch,
                "physical link must have one directed arc from each endpoint", std::nullopt,
                link.id);
        }
    }

    void validate_connectivity() {
        if (graph_.gpus().empty()) {
            return;
        }
        StructuralVisited visited{graph_};
        std::queue<NodeId> pending;
        const NodeId start{graph_.gpus().front().id};
        static_cast<void>(visited.mark(start));
        pending.push(start);
        while (!pending.empty()) {
            const NodeId current = pending.front();
            pending.pop();
            for (const DirectedLink& link : graph_.outgoing(current)) {
                const Port* destination = graph_.find(link.destination);
                if (destination != nullptr && visited.mark(destination->owner)) {
                    pending.push(destination->owner);
                }
            }
        }
        report_disconnected(visited);
    }

    void report_disconnected(const StructuralVisited& visited) {
        for (const GpuWorker& gpu : graph_.gpus()) {
            report_if_disconnected(visited, NodeId{gpu.id});
        }
        for (const Nic& nic : graph_.nics()) {
            report_if_disconnected(visited, NodeId{nic.id});
        }
        for (const Switch& switch_entity : graph_.switches()) {
            report_if_disconnected(visited, NodeId{switch_entity.id});
        }
    }

    void report_if_disconnected(const StructuralVisited& visited, NodeId node) {
        if (!visited.contains(node)) {
            add(ValidationErrorCode::DisconnectedNode,
                "topology node is disconnected from the GPU component", node);
        }
    }

    const TopologyGraph& graph_;
    std::vector<std::uint8_t> port_link_counts_;
    ValidationReport report_;
};

struct Connection final {
    NodeId source;
    NodeId destination;
};

[[nodiscard]] bool directly_connected(const TopologyGraph& graph, const Connection& connection) {
    return std::ranges::any_of(graph.outgoing(connection.source), [&](const DirectedLink& link) {
        const Port* destination_port = graph.find(link.destination);
        return destination_port != nullptr && destination_port->owner == connection.destination;
    });
}

[[nodiscard]] bool rack_matches_clos(const TopologyGraph& graph, const Rack& rack,
                                     const ClosConfig& config, std::span<const SwitchId> spines) {
    const std::size_t expected_gpu_count = config.nics_per_leaf * config.gpus_per_nic;
    if (rack.leaf_switches.size() != 1U || rack.nics.size() != config.nics_per_leaf ||
        rack.gpus.size() != expected_gpu_count) {
        return false;
    }

    const SwitchId leaf_id = rack.leaf_switches.front();
    const Switch* leaf = graph.find(leaf_id);
    if (leaf == nullptr || leaf->ports.size() != config.nics_per_leaf + config.spine_count) {
        return false;
    }
    for (NicId nic_id : rack.nics) {
        const Nic* nic = graph.find(nic_id);
        if (nic == nullptr || nic->attached_gpus.size() != config.gpus_per_nic ||
            !directly_connected(graph, Connection{NodeId{nic_id}, NodeId{leaf_id}})) {
            return false;
        }
    }
    return std::ranges::all_of(spines, [&](SwitchId spine_id) {
        return directly_connected(graph, Connection{NodeId{leaf_id}, NodeId{spine_id}});
    });
}

[[nodiscard]] bool matches_clos_shape(const TopologyGraph& graph, const ClosConfig& config,
                                      const ClosDimensions& expected) {
    if (graph.gpus().size() != expected.gpu_count || graph.nics().size() != expected.nic_count ||
        graph.racks().size() != expected.rack_count ||
        graph.switches().size() != expected.switch_count ||
        graph.ports().size() != expected.port_count ||
        graph.links().size() != expected.link_count) {
        return false;
    }

    std::vector<SwitchId> leaves;
    std::vector<SwitchId> spines;
    for (const Switch& switch_entity : graph.switches()) {
        (switch_entity.role == SwitchRole::Leaf ? leaves : spines).push_back(switch_entity.id);
    }
    if (leaves.size() != expected.leaf_count || spines.size() != expected.spine_count) {
        return false;
    }
    if (!std::ranges::all_of(spines, [&](SwitchId spine_id) {
            const Switch* spine = graph.find(spine_id);
            return spine != nullptr && spine->ports.size() == expected.leaf_count;
        })) {
        return false;
    }
    return std::ranges::all_of(graph.racks(), [&](const Rack& rack) {
        return rack_matches_clos(graph, rack, config, spines);
    });
}

} // namespace

bool ValidationReport::valid() const noexcept { return errors.empty(); }

ValidationReport validate_topology(const TopologyGraph& graph) {
    return TopologyValidator{graph}.validate();
}

ValidationReport validate_clos_topology(const TopologyGraph& graph, const ClosConfig& config) {
    const ClosDimensions expected = clos_dimensions(config);
    ValidationReport report = validate_topology(graph);
    if (!report.valid()) {
        return report;
    }

    if (!matches_clos_shape(graph, config, expected)) {
        report.errors.push_back(
            ValidationError{ValidationErrorCode::TopologyShapeMismatch, std::nullopt, std::nullopt,
                            "topology does not match the requested Clos profile"});
    }
    return report;
}

} // namespace nexuslab::topology
