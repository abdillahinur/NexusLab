// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/cli/application.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/topology/summary.hpp"

#include <gtest/gtest.h>

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace nexuslab::topology {
namespace {

TEST(TopologySummaryTest, CountsTopologyCompositionAndDownState) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    static_cast<void>(graph.add_gpu(nic));
    const SwitchId leaf = graph.add_leaf_switch(rack);
    const SwitchId spine = graph.add_spine_switch();
    static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink, NodeId{leaf},
                                           PortRole::FabricDownlink));
    const LinkId link = graph.connect_fabric(NodeId{leaf}, PortRole::FabricUplink, NodeId{spine},
                                             PortRole::FabricDownlink);
    const PhysicalLink* link_entity = graph.find(link);
    ASSERT_NE(link_entity, nullptr);
    ASSERT_TRUE(graph.set_switch_state(spine, OperationalState::Down));
    ASSERT_TRUE(graph.set_port_state(link_entity->endpoint_a, OperationalState::Down));
    ASSERT_TRUE(graph.set_link_state(link, OperationalState::Down));

    EXPECT_EQ(summarize_topology(graph), (TopologySummary{
                                             .racks = 1U,
                                             .gpus = 1U,
                                             .nics = 1U,
                                             .switches = 2U,
                                             .leaf_switches = 1U,
                                             .spine_switches = 1U,
                                             .ports = 6U,
                                             .links = 3U,
                                             .local_links = 1U,
                                             .fabric_links = 2U,
                                             .down_switches = 1U,
                                             .down_ports = 1U,
                                             .down_links = 1U,
                                         }));
}

TEST(TopologySummaryTest, FormatsStableKeyValueOutput) {
    const auto graph = generate_clos(ClosConfig{64U, 8U, 8U, 8U});

    EXPECT_EQ(format_topology_summary(summarize_topology(*graph), "clos:test"), "source=clos:test\n"
                                                                                "racks=1\n"
                                                                                "gpus=64\n"
                                                                                "nics=8\n"
                                                                                "switches=9\n"
                                                                                "leaf_switches=1\n"
                                                                                "spine_switches=8\n"
                                                                                "ports=160\n"
                                                                                "links=80\n"
                                                                                "local_links=64\n"
                                                                                "fabric_links=16\n"
                                                                                "down_switches=0\n"
                                                                                "down_ports=0\n"
                                                                                "down_links=0\n");
}

TEST(TopologySummaryCliTest, SummarizesApprovedStretchProfile) {
    constexpr std::array<std::string_view, 4> arguments{"topology", "summary", "--clos", "stretch"};
    std::ostringstream output;
    std::ostringstream error;

    EXPECT_EQ(cli::run(arguments, output, error), 0);
    EXPECT_NE(output.str().find("source=clos:stretch\n"), std::string::npos);
    EXPECT_NE(output.str().find("gpus=2048\n"), std::string::npos);
    EXPECT_TRUE(error.str().empty());
}

TEST(TopologySummaryCliTest, RejectsUnknownProfileAndInvalidSyntax) {
    constexpr std::array<std::string_view, 4> unknown_profile{"topology", "summary", "--clos",
                                                              "larger"};
    constexpr std::array<std::string_view, 3> incomplete{"topology", "summary", "--clos"};
    std::ostringstream output;
    std::ostringstream error;

    EXPECT_THROW(static_cast<void>(cli::run(unknown_profile, output, error)),
                 std::invalid_argument);
    EXPECT_EQ(cli::run(incomplete, output, error), 2);
    EXPECT_NE(error.str().find("nexuslab topology summary"), std::string::npos);
}

} // namespace
} // namespace nexuslab::topology
