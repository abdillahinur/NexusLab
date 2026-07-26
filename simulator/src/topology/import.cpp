// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/export.hpp"

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/validation.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nexuslab::topology {
namespace {

struct ParsedTopology final {
    std::vector<Rack> racks;
    std::vector<GpuWorker> gpus;
    std::vector<Nic> nics;
    std::vector<Switch> switches;
    std::vector<Port> ports;
    std::vector<PhysicalLink> links;
};

[[noreturn]] void fail(const std::string& message) {
    throw TopologyYamlError{"invalid topology YAML: " + message};
}

[[nodiscard]] YAML::Node require_field(const YAML::Node& map, const char* field) {
    if (!map.IsMap()) {
        fail("expected a mapping while reading field '" + std::string{field} + "'");
    }
    const YAML::Node value = map[field];
    if (!value.IsDefined()) {
        fail("missing required field '" + std::string{field} + "'");
    }
    return value;
}

[[nodiscard]] YAML::Node require_sequence(const YAML::Node& root, const char* field) {
    const YAML::Node sequence = require_field(root, field);
    if (!sequence.IsSequence()) {
        fail("field '" + std::string{field} + "' must be a sequence");
    }
    return sequence;
}

template <typename Id> [[nodiscard]] Id parse_id(const YAML::Node& node) {
    return Id{node.as<std::uint64_t>()};
}

template <typename Id>
[[nodiscard]] Id parse_dense_id(const YAML::Node& map, std::size_t expected,
                                const char* entity_kind) {
    const Id id = parse_id<Id>(require_field(map, "id"));
    if (id.value() != expected) {
        fail(std::string{entity_kind} + " IDs must be dense, ordered, and zero-based");
    }
    return id;
}

template <typename Id>
[[nodiscard]] std::vector<Id> parse_id_sequence(const YAML::Node& map, const char* field) {
    const YAML::Node sequence = require_field(map, field);
    if (!sequence.IsSequence()) {
        fail("field '" + std::string{field} + "' must be a sequence");
    }
    std::vector<Id> ids;
    ids.reserve(sequence.size());
    for (const YAML::Node& value : sequence) {
        ids.push_back(parse_id<Id>(value));
    }
    return ids;
}

[[nodiscard]] SwitchRole parse_switch_role(const YAML::Node& node) {
    const auto value = node.as<std::string>();
    if (value == "leaf") {
        return SwitchRole::Leaf;
    }
    if (value == "spine") {
        return SwitchRole::Spine;
    }
    fail("unknown switch role '" + value + "'");
}

[[nodiscard]] PortRole parse_port_role(const YAML::Node& node) {
    const auto value = node.as<std::string>();
    if (value == "local") {
        return PortRole::Local;
    }
    if (value == "fabric_downlink") {
        return PortRole::FabricDownlink;
    }
    if (value == "fabric_uplink") {
        return PortRole::FabricUplink;
    }
    fail("unknown port role '" + value + "'");
}

[[nodiscard]] LinkKind parse_link_kind(const YAML::Node& node) {
    const auto value = node.as<std::string>();
    if (value == "local_attachment") {
        return LinkKind::LocalAttachment;
    }
    if (value == "fabric") {
        return LinkKind::Fabric;
    }
    fail("unknown link kind '" + value + "'");
}

[[nodiscard]] OperationalState parse_operational_state(const YAML::Node& node) {
    const auto value = node.as<std::string>();
    if (value == "up") {
        return OperationalState::Up;
    }
    if (value == "down") {
        return OperationalState::Down;
    }
    fail("unknown operational state '" + value + "'");
}

[[nodiscard]] NodeId parse_node_id(const YAML::Node& node) {
    const auto kind = require_field(node, "kind").as<std::string>();
    const auto id = require_field(node, "id").as<std::uint64_t>();
    if (kind == "gpu") {
        return NodeId{GpuId{id}};
    }
    if (kind == "nic") {
        return NodeId{NicId{id}};
    }
    if (kind == "switch") {
        return NodeId{SwitchId{id}};
    }
    fail("unknown node kind '" + kind + "'");
}

[[nodiscard]] std::vector<Rack> parse_racks(const YAML::Node& root) {
    const YAML::Node sequence = require_sequence(root, "racks");
    std::vector<Rack> racks;
    racks.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const YAML::Node item = sequence[index];
        racks.push_back(Rack{parse_dense_id<RackId>(item, index, "rack"),
                             parse_id_sequence<GpuId>(item, "gpus"),
                             parse_id_sequence<NicId>(item, "nics"),
                             parse_id_sequence<SwitchId>(item, "leaf_switches")});
    }
    return racks;
}

[[nodiscard]] std::vector<GpuWorker> parse_gpus(const YAML::Node& root) {
    const YAML::Node sequence = require_sequence(root, "gpus");
    std::vector<GpuWorker> gpus;
    gpus.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const YAML::Node item = sequence[index];
        gpus.push_back(GpuWorker{parse_dense_id<GpuId>(item, index, "GPU"),
                                 parse_id<RackId>(require_field(item, "rack")),
                                 parse_id<NicId>(require_field(item, "attached_nic")),
                                 parse_id<PortId>(require_field(item, "port"))});
    }
    return gpus;
}

[[nodiscard]] std::vector<Nic> parse_nics(const YAML::Node& root) {
    const YAML::Node sequence = require_sequence(root, "nics");
    std::vector<Nic> nics;
    nics.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const YAML::Node item = sequence[index];
        nics.push_back(Nic{parse_dense_id<NicId>(item, index, "NIC"),
                           parse_id<RackId>(require_field(item, "rack")),
                           parse_id_sequence<GpuId>(item, "attached_gpus"),
                           parse_id_sequence<PortId>(item, "ports")});
    }
    return nics;
}

[[nodiscard]] std::optional<RackId> parse_optional_rack(const YAML::Node& item) {
    const YAML::Node rack = require_field(item, "rack");
    if (rack.IsNull()) {
        return std::nullopt;
    }
    return parse_id<RackId>(rack);
}

[[nodiscard]] std::vector<Switch> parse_switches(const YAML::Node& root) {
    const YAML::Node sequence = require_sequence(root, "switches");
    std::vector<Switch> switches;
    switches.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const YAML::Node item = sequence[index];
        switches.push_back(Switch{parse_dense_id<SwitchId>(item, index, "switch"),
                                  parse_optional_rack(item),
                                  parse_switch_role(require_field(item, "role")),
                                  parse_id_sequence<PortId>(item, "ports"),
                                  parse_operational_state(require_field(item, "state"))});
    }
    return switches;
}

[[nodiscard]] std::vector<Port> parse_ports(const YAML::Node& root) {
    const YAML::Node sequence = require_sequence(root, "ports");
    std::vector<Port> ports;
    ports.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const YAML::Node item = sequence[index];
        ports.push_back(Port{parse_dense_id<PortId>(item, index, "port"),
                             parse_node_id(require_field(item, "owner")),
                             parse_port_role(require_field(item, "role")),
                             parse_operational_state(require_field(item, "state"))});
    }
    return ports;
}

[[nodiscard]] std::vector<PhysicalLink> parse_links(const YAML::Node& root) {
    const YAML::Node sequence = require_sequence(root, "links");
    std::vector<PhysicalLink> links;
    links.reserve(sequence.size());
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const YAML::Node item = sequence[index];
        links.push_back(PhysicalLink{parse_dense_id<LinkId>(item, index, "link"),
                                     parse_id<PortId>(require_field(item, "endpoint_a")),
                                     parse_id<PortId>(require_field(item, "endpoint_b")),
                                     parse_link_kind(require_field(item, "kind")),
                                     parse_operational_state(require_field(item, "state"))});
    }
    return links;
}

[[nodiscard]] ParsedTopology parse_document(std::string_view yaml) {
    const YAML::Node root = YAML::Load(std::string{yaml});
    if (!root.IsMap()) {
        fail("document root must be a mapping");
    }
    const auto version = require_field(root, "schema_version").as<std::uint32_t>();
    if (version != topology_yaml_schema_version) {
        fail("unsupported schema_version " + std::to_string(version));
    }
    return ParsedTopology{parse_racks(root),    parse_gpus(root),  parse_nics(root),
                          parse_switches(root), parse_ports(root), parse_links(root)};
}

template <typename Id> void require_generated_id(Id actual, Id expected, const char* entity_kind) {
    if (actual != expected) {
        fail(std::string{"non-canonical "} + entity_kind + " construction order");
    }
}

[[nodiscard]] const Port& parsed_port(const ParsedTopology& parsed, PortId id) {
    if (id.value() >= parsed.ports.size()) {
        fail("link references an unknown port");
    }
    return parsed.ports[static_cast<std::size_t>(id.value())];
}

[[nodiscard]] const GpuWorker& parsed_gpu(const ParsedTopology& parsed, NodeId node) {
    if (node.kind() != NodeKind::Gpu || node.value() >= parsed.gpus.size()) {
        fail("local attachment must reference a known GPU");
    }
    return parsed.gpus[static_cast<std::size_t>(node.value())];
}

void create_racks_nics_and_switches(TopologyGraph& graph, const ParsedTopology& parsed) {
    for (const Rack& rack : parsed.racks) {
        require_generated_id(graph.add_rack(), rack.id, "rack");
    }
    for (const Nic& nic : parsed.nics) {
        require_generated_id(graph.add_nic(nic.rack), nic.id, "NIC");
    }
    for (const Switch& switch_entity : parsed.switches) {
        const SwitchId id =
            switch_entity.role == SwitchRole::Leaf
                ? graph.add_leaf_switch(switch_entity.rack.value_or(RackId{UINT64_MAX}))
                : graph.add_spine_switch();
        require_generated_id(id, switch_entity.id, "switch");
    }
}

void replay_local_attachment(TopologyGraph& graph, const ParsedTopology& parsed,
                             const PhysicalLink& link) {
    const Port& gpu_port = parsed_port(parsed, link.endpoint_a);
    const Port& nic_port = parsed_port(parsed, link.endpoint_b);
    if (gpu_port.owner.kind() != NodeKind::Gpu || nic_port.owner.kind() != NodeKind::Nic ||
        gpu_port.role != PortRole::Local || nic_port.role != PortRole::Local) {
        fail("local attachment endpoints are not in canonical GPU-to-NIC order");
    }
    const GpuWorker& gpu = parsed_gpu(parsed, gpu_port.owner);
    if (gpu.attached_nic.value() != nic_port.owner.value()) {
        fail("GPU attachment disagrees with local-link NIC endpoint");
    }
    require_generated_id(graph.add_gpu(gpu.attached_nic), gpu.id, "GPU");
    const PhysicalLink* generated = graph.find(link.id);
    if (generated == nullptr) {
        fail("local attachment did not produce the expected LinkId");
    }
}

void replay_fabric_link(TopologyGraph& graph, const ParsedTopology& parsed,
                        const PhysicalLink& link) {
    const Port& endpoint_a = parsed_port(parsed, link.endpoint_a);
    const Port& endpoint_b = parsed_port(parsed, link.endpoint_b);
    require_generated_id(
        graph.connect_fabric(endpoint_a.owner, endpoint_a.role, endpoint_b.owner, endpoint_b.role),
        link.id, "fabric link");
}

void replay_links(TopologyGraph& graph, const ParsedTopology& parsed) {
    for (const PhysicalLink& link : parsed.links) {
        if (link.kind == LinkKind::LocalAttachment) {
            replay_local_attachment(graph, parsed, link);
        } else {
            replay_fabric_link(graph, parsed, link);
        }
    }
}

void restore_operational_state(TopologyGraph& graph, const ParsedTopology& parsed) {
    for (const Switch& switch_entity : parsed.switches) {
        if (!graph.set_switch_state(switch_entity.id, switch_entity.state)) {
            fail("cannot restore state for an unknown switch");
        }
    }
    for (const Port& port : parsed.ports) {
        if (!graph.set_port_state(port.id, port.state)) {
            fail("cannot restore state for an unknown port");
        }
    }
    for (const PhysicalLink& link : parsed.links) {
        if (!graph.set_link_state(link.id, link.state)) {
            fail("cannot restore state for an unknown link");
        }
    }
}

template <typename Entity>
void require_equal_entities(std::span<const Entity> generated, const std::vector<Entity>& parsed,
                            const char* entity_kind) {
    if (!std::ranges::equal(generated, parsed)) {
        fail(std::string{"reconstructed "} + entity_kind +
             " entities do not match the canonical document");
    }
}

void require_exact_reconstruction(const TopologyGraph& graph, const ParsedTopology& parsed) {
    require_equal_entities(graph.racks(), parsed.racks, "rack");
    require_equal_entities(graph.gpus(), parsed.gpus, "GPU");
    require_equal_entities(graph.nics(), parsed.nics, "NIC");
    require_equal_entities(graph.switches(), parsed.switches, "switch");
    require_equal_entities(graph.ports(), parsed.ports, "port");
    require_equal_entities(graph.links(), parsed.links, "link");
    const ValidationReport report = validate_topology(graph);
    if (!report.valid()) {
        fail("reconstructed topology is invalid: " + report.errors.front().message);
    }
}

[[nodiscard]] std::unique_ptr<TopologyGraph> reconstruct(const ParsedTopology& parsed) {
    auto graph = std::make_unique<TopologyGraph>();
    create_racks_nics_and_switches(*graph, parsed);
    replay_links(*graph, parsed);
    restore_operational_state(*graph, parsed);
    require_exact_reconstruction(*graph, parsed);
    return graph;
}

} // namespace

std::unique_ptr<TopologyGraph> deserialize_topology_yaml(std::string_view yaml) {
    try {
        return reconstruct(parse_document(yaml));
    } catch (const TopologyYamlError&) {
        throw;
    } catch (const YAML::Exception& error) {
        throw TopologyYamlError{"invalid topology YAML: " + std::string{error.what()}};
    } catch (const std::exception& error) {
        throw TopologyYamlError{"invalid topology YAML: " + std::string{error.what()}};
    }
}

} // namespace nexuslab::topology
