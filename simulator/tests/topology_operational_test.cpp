// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/graph.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

namespace nexuslab::topology {
namespace {

struct TwoRackIds final {
    GpuId first_gpu;
    GpuId second_gpu;
    SwitchId spine;
    LinkId first_spine_link;
};

[[nodiscard]] TwoRackIds populate_two_rack_topology(TopologyGraph& graph) {
    const RackId first_rack = graph.add_rack();
    const RackId second_rack = graph.add_rack();
    const NicId first_nic = graph.add_nic(first_rack);
    const NicId second_nic = graph.add_nic(second_rack);
    const GpuId first_gpu = graph.add_gpu(first_nic);
    const GpuId second_gpu = graph.add_gpu(second_nic);
    const SwitchId first_leaf = graph.add_leaf_switch(first_rack);
    const SwitchId second_leaf = graph.add_leaf_switch(second_rack);
    const SwitchId spine = graph.add_spine_switch();

    static_cast<void>(graph.connect_fabric(NodeId{first_nic}, PortRole::FabricUplink,
                                           NodeId{first_leaf}, PortRole::FabricDownlink));
    static_cast<void>(graph.connect_fabric(NodeId{second_nic}, PortRole::FabricUplink,
                                           NodeId{second_leaf}, PortRole::FabricDownlink));
    const LinkId first_spine_link = graph.connect_fabric(NodeId{first_leaf}, PortRole::FabricUplink,
                                                         NodeId{spine}, PortRole::FabricDownlink);
    static_cast<void>(graph.connect_fabric(NodeId{second_leaf}, PortRole::FabricUplink,
                                           NodeId{spine}, PortRole::FabricDownlink));

    return TwoRackIds{first_gpu, second_gpu, spine, first_spine_link};
}

TEST(TopologyOperationalTest, FindsDeterministicShortestHopReachability) {
    TopologyGraph graph;
    const TwoRackIds ids = populate_two_rack_topology(graph);

    EXPECT_EQ(graph.shortest_hops(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}),
              std::optional<std::size_t>{6U});
    EXPECT_TRUE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));
    EXPECT_EQ(graph.shortest_hops(NodeId{ids.first_gpu}, NodeId{ids.first_gpu}),
              std::optional<std::size_t>{0U});

    DirectedLink invalid_arc = graph.outgoing(NodeId{ids.first_gpu}).front();
    invalid_arc.destination = invalid_arc.source;
    EXPECT_FALSE(graph.is_operational(invalid_arc));
}

TEST(TopologyOperationalTest, LinkFailureAndRecoveryPreserveGraphStructure) {
    TopologyGraph graph;
    const TwoRackIds ids = populate_two_rack_topology(graph);
    const std::vector<PhysicalLink> original_links{graph.links().begin(), graph.links().end()};
    const std::vector<Port> original_ports{graph.ports().begin(), graph.ports().end()};
    const std::vector<DirectedLink> original_adjacency{graph.outgoing(NodeId{ids.spine}).begin(),
                                                       graph.outgoing(NodeId{ids.spine}).end()};

    ASSERT_TRUE(graph.set_link_state(ids.first_spine_link, OperationalState::Down));
    EXPECT_FALSE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));
    ASSERT_TRUE(graph.set_link_state(ids.first_spine_link, OperationalState::Up));

    EXPECT_TRUE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));
    EXPECT_EQ(std::vector<PhysicalLink>(graph.links().begin(), graph.links().end()),
              original_links);
    EXPECT_EQ(std::vector<Port>(graph.ports().begin(), graph.ports().end()), original_ports);
    EXPECT_EQ(std::vector<DirectedLink>(graph.outgoing(NodeId{ids.spine}).begin(),
                                        graph.outgoing(NodeId{ids.spine}).end()),
              original_adjacency);
}

TEST(TopologyOperationalTest, PortFailureRemovesBothDirectionsUntilRecovery) {
    TopologyGraph graph;
    const TwoRackIds ids = populate_two_rack_topology(graph);
    const PhysicalLink* link = graph.find(ids.first_spine_link);
    ASSERT_NE(link, nullptr);
    const PortId failed_port = link->endpoint_a;
    const Port* port = graph.find(failed_port);
    ASSERT_NE(port, nullptr);
    const DirectedLink directed_link = graph.outgoing(port->owner).back();

    ASSERT_TRUE(graph.set_port_state(failed_port, OperationalState::Down));
    EXPECT_FALSE(graph.is_operational(directed_link));
    EXPECT_FALSE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));

    ASSERT_TRUE(graph.set_port_state(failed_port, OperationalState::Up));
    EXPECT_TRUE(graph.is_operational(directed_link));
    EXPECT_TRUE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));
}

TEST(TopologyOperationalTest, SwitchFailureRemovesTransitUntilRecovery) {
    TopologyGraph graph;
    const TwoRackIds ids = populate_two_rack_topology(graph);

    ASSERT_TRUE(graph.set_switch_state(ids.spine, OperationalState::Down));
    EXPECT_FALSE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));
    EXPECT_EQ(graph.shortest_hops(NodeId{ids.spine}, NodeId{ids.spine}), std::nullopt);

    ASSERT_TRUE(graph.set_switch_state(ids.spine, OperationalState::Up));
    EXPECT_TRUE(graph.reachable(NodeId{ids.first_gpu}, NodeId{ids.second_gpu}));
}

TEST(TopologyOperationalTest, RejectsUnknownStateAndPathReferences) {
    TopologyGraph graph;

    EXPECT_FALSE(graph.set_link_state(LinkId{99}, OperationalState::Down));
    EXPECT_FALSE(graph.set_port_state(PortId{99}, OperationalState::Down));
    EXPECT_FALSE(graph.set_switch_state(SwitchId{99}, OperationalState::Down));
    EXPECT_THROW(static_cast<void>(graph.shortest_hops(NodeId{GpuId{99}}, NodeId{GpuId{100}})),
                 std::invalid_argument);
}

} // namespace
} // namespace nexuslab::topology
