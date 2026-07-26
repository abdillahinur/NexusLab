// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/export.hpp"
#include "nexuslab/topology/graph.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace nexuslab::topology {
namespace {

struct ExportTopologyIds final {
    SwitchId spine;
    LinkId spine_link;
    PortId spine_link_port;
};

[[nodiscard]] ExportTopologyIds populate_export_topology(TopologyGraph& graph) {
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    static_cast<void>(graph.add_gpu(nic));
    const SwitchId leaf = graph.add_leaf_switch(rack);
    const SwitchId spine = graph.add_spine_switch();
    static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink, NodeId{leaf},
                                           PortRole::FabricDownlink));
    const LinkId spine_link = graph.connect_fabric(NodeId{leaf}, PortRole::FabricUplink,
                                                   NodeId{spine}, PortRole::FabricDownlink);
    const PhysicalLink* link = graph.find(spine_link);
    if (link == nullptr) {
        throw std::logic_error{"newly created spine link is unavailable"};
    }
    return ExportTopologyIds{spine, spine_link, link->endpoint_a};
}

TEST(TopologyExportTest, EmitsVersionedOrderedYamlSchema) {
    TopologyGraph graph;
    static_cast<void>(populate_export_topology(graph));

    const std::string yaml = serialize_topology_yaml(graph);
    const YAML::Node root = YAML::Load(yaml);

    EXPECT_EQ(root["schema_version"].as<std::uint32_t>(), topology_yaml_schema_version);
    EXPECT_EQ(root["racks"].size(), 1U);
    EXPECT_EQ(root["gpus"].size(), 1U);
    EXPECT_EQ(root["nics"].size(), 1U);
    EXPECT_EQ(root["switches"].size(), 2U);
    EXPECT_EQ(root["ports"].size(), 6U);
    EXPECT_EQ(root["links"].size(), 3U);
    EXPECT_FALSE(root["adjacency"]);
    EXPECT_TRUE(root["switches"][1]["rack"].IsNull());
    EXPECT_EQ(root["ports"][0]["owner"]["kind"].as<std::string>(), "gpu");
    EXPECT_EQ(root["links"][0]["kind"].as<std::string>(), "local_attachment");
}

TEST(TopologyExportTest, PreservesOperationalStateInYaml) {
    TopologyGraph graph;
    const ExportTopologyIds ids = populate_export_topology(graph);
    ASSERT_TRUE(graph.set_switch_state(ids.spine, OperationalState::Down));
    ASSERT_TRUE(graph.set_link_state(ids.spine_link, OperationalState::Down));
    ASSERT_TRUE(graph.set_port_state(ids.spine_link_port, OperationalState::Down));

    const YAML::Node root = YAML::Load(serialize_topology_yaml(graph));

    EXPECT_EQ(root["switches"][1]["state"].as<std::string>(), "down");
    EXPECT_EQ(root["links"][2]["state"].as<std::string>(), "down");
    EXPECT_EQ(root["ports"][ids.spine_link_port.value()]["state"].as<std::string>(), "down");
}

TEST(TopologyExportTest, YamlIsByteStableAcrossIndependentCanonicalGraphs) {
    const ClosConfig config{64U, 8U, 8U, 8U};
    const std::unique_ptr<TopologyGraph> first = generate_clos(config);
    const std::unique_ptr<TopologyGraph> second = generate_clos(config);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    const std::string first_export = serialize_topology_yaml(*first);

    EXPECT_EQ(first_export, serialize_topology_yaml(*first));
    EXPECT_EQ(first_export, serialize_topology_yaml(*second));
    EXPECT_EQ(first_export.back(), '\n');
}

TEST(TopologyExportTest, EmitsDeterministicGraphvizForDirectAttachment) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    static_cast<void>(graph.add_gpu(nic));

    EXPECT_EQ(export_topology_dot(graph),
              "graph NexusLabTopology {\n"
              "    graph [rankdir=TB];\n"
              "    node [fontname=\"Helvetica\"];\n"
              "    edge [fontname=\"Helvetica\"];\n"
              "    subgraph cluster_rack_0 {\n"
              "        label=\"Rack 0\";\n"
              "        color=\"#b0b0b0\";\n"
              "        gpu_0 [label=\"GPU 0\", shape=box];\n"
              "        nic_0 [label=\"NIC 0\", shape=ellipse];\n"
              "    }\n"
              "    gpu_0 -- nic_0 [label=\"Link 0: local_attachment\"];\n"
              "}\n");
}

TEST(TopologyExportTest, MarksFailedSwitchesAndLinksInGraphviz) {
    TopologyGraph graph;
    const ExportTopologyIds ids = populate_export_topology(graph);
    ASSERT_TRUE(graph.set_switch_state(ids.spine, OperationalState::Down));

    const std::string dot = export_topology_dot(graph);

    EXPECT_NE(dot.find("switch_1 [label=\"Spine 1\", shape=hexagon, color=\"#d62728\", "
                       "penwidth=2]"),
              std::string::npos);
    EXPECT_NE(dot.find("style=dashed, color=\"#d62728\""), std::string::npos);
    EXPECT_EQ(dot, export_topology_dot(graph));
}

TEST(TopologyExportTest, RejectsStructurallyInvalidTopology) {
    const TopologyGraph graph;

    EXPECT_THROW(static_cast<void>(serialize_topology_yaml(graph)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(export_topology_dot(graph)), std::invalid_argument);
}

} // namespace
} // namespace nexuslab::topology
