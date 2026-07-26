// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/export.hpp"

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/validation.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace nexuslab::topology {
namespace {

[[nodiscard]] const char* node_kind_name(NodeKind kind) {
    switch (kind) {
    case NodeKind::Gpu:
        return "gpu";
    case NodeKind::Nic:
        return "nic";
    case NodeKind::Switch:
        return "switch";
    }
    throw std::logic_error{"unknown topology node kind"};
}

[[nodiscard]] const char* switch_role_name(SwitchRole role) {
    switch (role) {
    case SwitchRole::Leaf:
        return "leaf";
    case SwitchRole::Spine:
        return "spine";
    }
    throw std::logic_error{"unknown topology switch role"};
}

[[nodiscard]] const char* port_role_name(PortRole role) {
    switch (role) {
    case PortRole::Local:
        return "local";
    case PortRole::FabricDownlink:
        return "fabric_downlink";
    case PortRole::FabricUplink:
        return "fabric_uplink";
    }
    throw std::logic_error{"unknown topology port role"};
}

[[nodiscard]] const char* link_kind_name(LinkKind kind) {
    switch (kind) {
    case LinkKind::LocalAttachment:
        return "local_attachment";
    case LinkKind::Fabric:
        return "fabric";
    }
    throw std::logic_error{"unknown topology link kind"};
}

[[nodiscard]] const char* operational_state_name(OperationalState state) {
    switch (state) {
    case OperationalState::Up:
        return "up";
    case OperationalState::Down:
        return "down";
    }
    throw std::logic_error{"unknown topology operational state"};
}

void require_valid_topology(const TopologyGraph& graph) {
    const ValidationReport report = validate_topology(graph);
    if (!report.valid()) {
        throw std::invalid_argument{"cannot export invalid topology: " +
                                    report.errors.front().message};
    }
}

template <typename Id> void emit_id_sequence(YAML::Emitter& output, const std::vector<Id>& ids) {
    output << YAML::Flow << YAML::BeginSeq;
    for (Id id : ids) {
        output << id.value();
    }
    output << YAML::EndSeq;
}

void emit_racks(YAML::Emitter& output, const TopologyGraph& graph) {
    output << YAML::Key << "racks" << YAML::Value << YAML::Block << YAML::BeginSeq;
    for (const Rack& rack : graph.racks()) {
        output << YAML::BeginMap;
        output << YAML::Key << "id" << YAML::Value << rack.id.value();
        output << YAML::Key << "gpus" << YAML::Value;
        emit_id_sequence(output, rack.gpus);
        output << YAML::Key << "nics" << YAML::Value;
        emit_id_sequence(output, rack.nics);
        output << YAML::Key << "leaf_switches" << YAML::Value;
        emit_id_sequence(output, rack.leaf_switches);
        output << YAML::EndMap;
    }
    output << YAML::EndSeq;
}

void emit_gpus(YAML::Emitter& output, const TopologyGraph& graph) {
    output << YAML::Key << "gpus" << YAML::Value << YAML::Block << YAML::BeginSeq;
    for (const GpuWorker& gpu : graph.gpus()) {
        output << YAML::BeginMap;
        output << YAML::Key << "id" << YAML::Value << gpu.id.value();
        output << YAML::Key << "rack" << YAML::Value << gpu.rack.value();
        output << YAML::Key << "attached_nic" << YAML::Value << gpu.attached_nic.value();
        output << YAML::Key << "port" << YAML::Value << gpu.port.value();
        output << YAML::EndMap;
    }
    output << YAML::EndSeq;
}

void emit_nics(YAML::Emitter& output, const TopologyGraph& graph) {
    output << YAML::Key << "nics" << YAML::Value << YAML::Block << YAML::BeginSeq;
    for (const Nic& nic : graph.nics()) {
        output << YAML::BeginMap;
        output << YAML::Key << "id" << YAML::Value << nic.id.value();
        output << YAML::Key << "rack" << YAML::Value << nic.rack.value();
        output << YAML::Key << "attached_gpus" << YAML::Value;
        emit_id_sequence(output, nic.attached_gpus);
        output << YAML::Key << "ports" << YAML::Value;
        emit_id_sequence(output, nic.ports);
        output << YAML::EndMap;
    }
    output << YAML::EndSeq;
}

void emit_switches(YAML::Emitter& output, const TopologyGraph& graph) {
    output << YAML::Key << "switches" << YAML::Value << YAML::Block << YAML::BeginSeq;
    for (const Switch& switch_entity : graph.switches()) {
        output << YAML::BeginMap;
        output << YAML::Key << "id" << YAML::Value << switch_entity.id.value();
        output << YAML::Key << "rack" << YAML::Value;
        if (switch_entity.rack.has_value()) {
            output << switch_entity.rack->value();
        } else {
            output << YAML::Null;
        }
        output << YAML::Key << "role" << YAML::Value << switch_role_name(switch_entity.role);
        output << YAML::Key << "state" << YAML::Value
               << operational_state_name(switch_entity.state);
        output << YAML::Key << "ports" << YAML::Value;
        emit_id_sequence(output, switch_entity.ports);
        output << YAML::EndMap;
    }
    output << YAML::EndSeq;
}

void emit_ports(YAML::Emitter& output, const TopologyGraph& graph) {
    output << YAML::Key << "ports" << YAML::Value << YAML::Block << YAML::BeginSeq;
    for (const Port& port : graph.ports()) {
        output << YAML::BeginMap;
        output << YAML::Key << "id" << YAML::Value << port.id.value();
        output << YAML::Key << "owner" << YAML::Value << YAML::BeginMap;
        output << YAML::Key << "kind" << YAML::Value << node_kind_name(port.owner.kind());
        output << YAML::Key << "id" << YAML::Value << port.owner.value();
        output << YAML::EndMap;
        output << YAML::Key << "role" << YAML::Value << port_role_name(port.role);
        output << YAML::Key << "state" << YAML::Value << operational_state_name(port.state);
        output << YAML::EndMap;
    }
    output << YAML::EndSeq;
}

void emit_links(YAML::Emitter& output, const TopologyGraph& graph) {
    output << YAML::Key << "links" << YAML::Value << YAML::Block << YAML::BeginSeq;
    for (const PhysicalLink& link : graph.links()) {
        output << YAML::BeginMap;
        output << YAML::Key << "id" << YAML::Value << link.id.value();
        output << YAML::Key << "endpoint_a" << YAML::Value << link.endpoint_a.value();
        output << YAML::Key << "endpoint_b" << YAML::Value << link.endpoint_b.value();
        output << YAML::Key << "kind" << YAML::Value << link_kind_name(link.kind);
        output << YAML::Key << "state" << YAML::Value << operational_state_name(link.state);
        output << YAML::EndMap;
    }
    output << YAML::EndSeq;
}

[[nodiscard]] std::string dot_node_name(NodeId node) {
    return std::string{node_kind_name(node.kind())} + '_' + std::to_string(node.value());
}

void emit_gpu_dot(std::ostringstream& output, const GpuWorker& gpu) {
    output << "        " << dot_node_name(NodeId{gpu.id}) << " [label=\"GPU " << gpu.id.value()
           << "\", shape=box];\n";
}

void emit_nic_dot(std::ostringstream& output, const Nic& nic) {
    output << "        " << dot_node_name(NodeId{nic.id}) << " [label=\"NIC " << nic.id.value()
           << "\", shape=ellipse];\n";
}

void emit_switch_dot(std::ostringstream& output, const Switch& switch_entity,
                     const char* indentation) {
    output << indentation << dot_node_name(NodeId{switch_entity.id}) << " [label=\""
           << (switch_entity.role == SwitchRole::Leaf ? "Leaf " : "Spine ")
           << switch_entity.id.value()
           << "\", shape=" << (switch_entity.role == SwitchRole::Leaf ? "diamond" : "hexagon");
    if (switch_entity.state == OperationalState::Down) {
        output << ", color=\"#d62728\", penwidth=2";
    }
    output << "];\n";
}

void emit_rack_dot(std::ostringstream& output, const TopologyGraph& graph, const Rack& rack) {
    output << "    subgraph cluster_rack_" << rack.id.value() << " {\n";
    output << "        label=\"Rack " << rack.id.value() << "\";\n";
    output << "        color=\"#b0b0b0\";\n";
    for (GpuId gpu_id : rack.gpus) {
        emit_gpu_dot(output, *graph.find(gpu_id));
    }
    for (NicId nic_id : rack.nics) {
        emit_nic_dot(output, *graph.find(nic_id));
    }
    for (SwitchId switch_id : rack.leaf_switches) {
        emit_switch_dot(output, *graph.find(switch_id), "        ");
    }
    output << "    }\n";
}

void emit_link_dot(std::ostringstream& output, const TopologyGraph& graph,
                   const PhysicalLink& link) {
    const Port* endpoint_a = graph.find(link.endpoint_a);
    const Port* endpoint_b = graph.find(link.endpoint_b);
    if (endpoint_a == nullptr || endpoint_b == nullptr) {
        throw std::logic_error{"validated topology link has an unknown endpoint"};
    }
    const auto directions = directed_links(link);
    const bool operational =
        graph.is_operational(directions[0]) && graph.is_operational(directions[1]);
    output << "    " << dot_node_name(endpoint_a->owner) << " -- "
           << dot_node_name(endpoint_b->owner) << " [label=\"Link " << link.id.value() << ": "
           << link_kind_name(link.kind) << '"';
    if (!operational) {
        output << ", style=dashed, color=\"#d62728\"";
    }
    output << "];\n";
}

} // namespace

std::string serialize_topology_yaml(const TopologyGraph& graph) {
    require_valid_topology(graph);
    YAML::Emitter output;
    output << YAML::BeginMap;
    output << YAML::Key << "schema_version" << YAML::Value << topology_yaml_schema_version;
    emit_racks(output, graph);
    emit_gpus(output, graph);
    emit_nics(output, graph);
    emit_switches(output, graph);
    emit_ports(output, graph);
    emit_links(output, graph);
    output << YAML::EndMap;
    if (!output.good()) {
        throw std::runtime_error{"failed to serialize topology YAML"};
    }
    return std::string{output.c_str()} + '\n';
}

std::string export_topology_dot(const TopologyGraph& graph) {
    require_valid_topology(graph);
    std::ostringstream output;
    output << "graph NexusLabTopology {\n";
    output << "    graph [rankdir=TB];\n";
    output << "    node [fontname=\"Helvetica\"];\n";
    output << "    edge [fontname=\"Helvetica\"];\n";
    for (const Rack& rack : graph.racks()) {
        emit_rack_dot(output, graph, rack);
    }
    for (const Switch& switch_entity : graph.switches()) {
        if (switch_entity.role == SwitchRole::Spine) {
            emit_switch_dot(output, switch_entity, "    ");
        }
    }
    for (const PhysicalLink& link : graph.links()) {
        emit_link_dot(output, graph, link);
    }
    output << "}\n";
    return output.str();
}

} // namespace nexuslab::topology
