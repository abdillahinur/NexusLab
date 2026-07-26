// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/export.hpp"
#include "nexuslab/topology/graph.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace nexuslab::topology {
namespace {

template <typename Entity>
void expect_equal_entities(std::span<const Entity> left, std::span<const Entity> right) {
    EXPECT_TRUE(std::ranges::equal(left, right));
}

void expect_equal_graphs(const TopologyGraph& left, const TopologyGraph& right) {
    expect_equal_entities(left.racks(), right.racks());
    expect_equal_entities(left.gpus(), right.gpus());
    expect_equal_entities(left.nics(), right.nics());
    expect_equal_entities(left.switches(), right.switches());
    expect_equal_entities(left.ports(), right.ports());
    expect_equal_entities(left.links(), right.links());
}

struct StatefulTopologyIds final {
    SwitchId spine;
    LinkId spine_link;
    PortId leaf_port;
};

[[nodiscard]] StatefulTopologyIds populate_stateful_topology(TopologyGraph& graph) {
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
    EXPECT_NE(link, nullptr);
    return StatefulTopologyIds{spine, spine_link, link == nullptr ? PortId{0} : link->endpoint_a};
}

[[nodiscard]] std::string mutate_yaml(const std::string& yaml, const auto& mutation) {
    YAML::Node root = YAML::Load(yaml);
    mutation(root);
    return YAML::Dump(root);
}

void expect_invalid_yaml(std::string_view yaml) {
    EXPECT_THROW(static_cast<void>(deserialize_topology_yaml(yaml)), TopologyYamlError);
}

TEST(TopologyImportTest, CanonicalClosRoundTripIsByteStable) {
    const std::unique_ptr<TopologyGraph> original = generate_clos(ClosConfig{64U, 8U, 8U, 8U});
    ASSERT_NE(original, nullptr);
    const std::string canonical = serialize_topology_yaml(*original);

    const std::unique_ptr<TopologyGraph> restored = deserialize_topology_yaml(canonical);

    ASSERT_NE(restored, nullptr);
    expect_equal_graphs(*original, *restored);
    EXPECT_EQ(serialize_topology_yaml(*restored), canonical);
    EXPECT_EQ(export_topology_dot(*restored), export_topology_dot(*original));
}

TEST(TopologyImportTest, RoundTripRestoresOperationalState) {
    TopologyGraph original;
    const StatefulTopologyIds ids = populate_stateful_topology(original);
    ASSERT_TRUE(original.set_switch_state(ids.spine, OperationalState::Down));
    ASSERT_TRUE(original.set_link_state(ids.spine_link, OperationalState::Down));
    ASSERT_TRUE(original.set_port_state(ids.leaf_port, OperationalState::Down));
    const std::string canonical = serialize_topology_yaml(original);

    const std::unique_ptr<TopologyGraph> restored = deserialize_topology_yaml(canonical);

    ASSERT_NE(restored, nullptr);
    expect_equal_graphs(original, *restored);
    EXPECT_EQ(serialize_topology_yaml(*restored), canonical);
}

TEST(TopologyImportTest, RejectsMalformedYamlAndUnsupportedSchema) {
    expect_invalid_yaml("[");

    const std::unique_ptr<TopologyGraph> graph = generate_clos(ClosConfig{64U, 8U, 8U, 8U});
    ASSERT_NE(graph, nullptr);
    const std::string unsupported =
        mutate_yaml(serialize_topology_yaml(*graph), [](YAML::Node& root) {
            root["schema_version"] = topology_yaml_schema_version + 1U;
        });

    expect_invalid_yaml(unsupported);
}

TEST(TopologyImportTest, RejectsMissingRequiredArraysAndUnknownEnums) {
    TopologyGraph graph;
    static_cast<void>(populate_stateful_topology(graph));
    const std::string canonical = serialize_topology_yaml(graph);
    const std::string missing_links =
        mutate_yaml(canonical, [](YAML::Node& root) { root.remove("links"); });
    const std::string unknown_role =
        mutate_yaml(canonical, [](YAML::Node& root) { root["switches"][0]["role"] = "tor"; });

    expect_invalid_yaml(missing_links);
    expect_invalid_yaml(unknown_role);
}

TEST(TopologyImportTest, RejectsNonDenseIdsAndBrokenRelationships) {
    TopologyGraph graph;
    static_cast<void>(populate_stateful_topology(graph));
    const std::string canonical = serialize_topology_yaml(graph);
    const std::string non_dense =
        mutate_yaml(canonical, [](YAML::Node& root) { root["gpus"][0]["id"] = 7U; });
    const std::string broken_attachment =
        mutate_yaml(canonical, [](YAML::Node& root) { root["gpus"][0]["attached_nic"] = 99U; });

    expect_invalid_yaml(non_dense);
    expect_invalid_yaml(broken_attachment);
}

TEST(TopologyImportTest, RejectsNonCanonicalLocalLinkOrientation) {
    TopologyGraph graph;
    static_cast<void>(populate_stateful_topology(graph));
    const std::string reversed = mutate_yaml(serialize_topology_yaml(graph), [](YAML::Node& root) {
        const auto endpoint_a = root["links"][0]["endpoint_a"].as<std::uint64_t>();
        root["links"][0]["endpoint_a"] = root["links"][0]["endpoint_b"];
        root["links"][0]["endpoint_b"] = endpoint_a;
    });

    expect_invalid_yaml(reversed);
}

} // namespace
} // namespace nexuslab::topology
