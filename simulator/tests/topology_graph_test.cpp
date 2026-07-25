// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/graph.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <tuple>
#include <vector>

namespace nexuslab::topology {
namespace {

TEST(TopologyGraphTest, BuildsRackNicGpuAndLocalAttachment) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    const GpuId gpu = graph.add_gpu(nic);

    ASSERT_NE(graph.find(gpu), nullptr);
    ASSERT_NE(graph.find(nic), nullptr);
    ASSERT_NE(graph.find(rack), nullptr);
    EXPECT_EQ((std::tuple{graph.find(gpu)->rack, graph.find(gpu)->attached_nic,
                          graph.find(nic)->attached_gpus, graph.find(rack)->gpus}),
              (std::tuple{rack, nic, std::vector{gpu}, std::vector{gpu}}));
    EXPECT_EQ((std::tuple{graph.gpus().size(), graph.nics().size(), graph.racks().size(),
                          graph.ports().size(), graph.links().size()}),
              (std::tuple{1U, 1U, 1U, 2U, 1U}));
    EXPECT_EQ(graph.links()[0].kind, LinkKind::LocalAttachment);
    EXPECT_EQ(graph.outgoing(NodeId{gpu}).size(), 1U);
    EXPECT_EQ(graph.outgoing(NodeId{nic}).size(), 1U);
}

TEST(TopologyGraphTest, AssignsDenseDeterministicIdsPerEntityKind) {
    TopologyGraph graph;
    const RackId first_rack = graph.add_rack();
    const RackId second_rack = graph.add_rack();
    const NicId first_nic = graph.add_nic(first_rack);
    const NicId second_nic = graph.add_nic(second_rack);
    const SwitchId first_leaf = graph.add_leaf_switch(first_rack);
    const SwitchId first_spine = graph.add_spine_switch();

    EXPECT_EQ((std::tuple{first_rack, second_rack, first_nic, second_nic, first_leaf, first_spine}),
              (std::tuple{RackId{0}, RackId{1}, NicId{0}, NicId{1}, SwitchId{0}, SwitchId{1}}));
}

TEST(TopologyGraphTest, ConnectsFabricNodesWithOwnedPortsAndDirectedAdjacency) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    const SwitchId leaf = graph.add_leaf_switch(rack);
    const SwitchId spine = graph.add_spine_switch();

    const LinkId downlink = graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink, NodeId{leaf},
                                                 PortRole::FabricDownlink);
    const LinkId uplink = graph.connect_fabric(NodeId{leaf}, PortRole::FabricUplink, NodeId{spine},
                                               PortRole::FabricDownlink);

    ASSERT_NE(graph.find(downlink), nullptr);
    ASSERT_NE(graph.find(uplink), nullptr);
    ASSERT_NE(graph.find(nic), nullptr);
    ASSERT_NE(graph.find(leaf), nullptr);
    ASSERT_NE(graph.find(spine), nullptr);
    EXPECT_EQ((std::tuple{graph.find(nic)->ports.size(), graph.find(leaf)->ports.size(),
                          graph.find(spine)->ports.size()}),
              (std::tuple{1U, 2U, 1U}));
    EXPECT_EQ((std::tuple{graph.outgoing(NodeId{nic}).size(), graph.outgoing(NodeId{leaf}).size(),
                          graph.outgoing(NodeId{spine}).size()}),
              (std::tuple{1U, 2U, 1U}));
    EXPECT_EQ(graph.outgoing(NodeId{leaf})[0].id.link, downlink);
    EXPECT_EQ(graph.outgoing(NodeId{leaf})[1].id.link, uplink);
}

TEST(TopologyGraphTest, RejectsUnknownConstructionReferences) {
    TopologyGraph graph;

    EXPECT_THROW(static_cast<void>(graph.add_nic(RackId{0})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(graph.add_gpu(NicId{0})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(graph.add_leaf_switch(RackId{0})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(graph.outgoing(NodeId{NicId{0}})), std::out_of_range);
}

TEST(TopologyGraphTest, RejectsInvalidFabricConnections) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    const GpuId gpu = graph.add_gpu(nic);
    const SwitchId leaf = graph.add_leaf_switch(rack);

    EXPECT_THROW(
        static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink,
                                               NodeId{SwitchId{99}}, PortRole::FabricDownlink)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink,
                                                        NodeId{nic}, PortRole::FabricDownlink)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(graph.connect_fabric(NodeId{gpu}, PortRole::FabricUplink,
                                                        NodeId{leaf}, PortRole::FabricDownlink)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::Local, NodeId{leaf},
                                                        PortRole::FabricDownlink)),
                 std::invalid_argument);
}

TEST(TopologyGraphTest, RejectsParallelPhysicalLinks) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    const SwitchId leaf = graph.add_leaf_switch(rack);
    static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink, NodeId{leaf},
                                           PortRole::FabricDownlink));

    EXPECT_THROW(static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink,
                                                        NodeId{leaf}, PortRole::FabricDownlink)),
                 std::invalid_argument);
}

TEST(TopologyGraphTest, UnknownDenseIdsHaveNoLookupResult) {
    TopologyGraph graph;

    EXPECT_EQ((std::tuple{graph.find(GpuId{0}), graph.find(NicId{0}), graph.find(SwitchId{0}),
                          graph.find(RackId{0}), graph.find(PortId{0}), graph.find(LinkId{0})}),
              (std::tuple{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
}

} // namespace
} // namespace nexuslab::topology
