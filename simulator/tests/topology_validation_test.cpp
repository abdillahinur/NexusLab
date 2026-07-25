// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/graph.hpp"
#include "nexuslab/topology/validation.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace nexuslab::topology {
namespace {

[[nodiscard]] bool has_error(const ValidationReport& report, ValidationErrorCode code) {
    return std::ranges::any_of(report.errors,
                               [code](const ValidationError& error) { return error.code == code; });
}

struct ValidTopologyIds final {
    SwitchId spine;
    LinkId spine_link;
};

[[nodiscard]] ValidTopologyIds populate_valid_topology(TopologyGraph& graph) {
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    static_cast<void>(graph.add_gpu(nic));
    const SwitchId leaf = graph.add_leaf_switch(rack);
    const SwitchId spine = graph.add_spine_switch();
    static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink, NodeId{leaf},
                                           PortRole::FabricDownlink));
    const LinkId spine_link = graph.connect_fabric(NodeId{leaf}, PortRole::FabricUplink,
                                                   NodeId{spine}, PortRole::FabricDownlink);
    return ValidTopologyIds{spine, spine_link};
}

TEST(TopologyValidationTest, AcceptsCompleteConnectedTopology) {
    TopologyGraph graph;
    static_cast<void>(populate_valid_topology(graph));

    const ValidationReport report = validate_topology(graph);

    EXPECT_TRUE(report.valid());
    EXPECT_TRUE(report.errors.empty());
}

TEST(TopologyValidationTest, OperationalFailuresDoNotInvalidateStructure) {
    TopologyGraph graph;
    const ValidTopologyIds ids = populate_valid_topology(graph);
    const PhysicalLink* spine_link = graph.find(ids.spine_link);
    ASSERT_NE(spine_link, nullptr);
    const PortId failed_port = spine_link->endpoint_a;

    ASSERT_TRUE(graph.set_link_state(ids.spine_link, OperationalState::Down));
    ASSERT_TRUE(graph.set_port_state(failed_port, OperationalState::Down));
    ASSERT_TRUE(graph.set_switch_state(ids.spine, OperationalState::Down));

    EXPECT_TRUE(validate_topology(graph).valid());
}

TEST(TopologyValidationTest, ReportsEmptyTopologyWithStructuredCode) {
    const TopologyGraph graph;

    const ValidationReport report = validate_topology(graph);

    EXPECT_FALSE(report.valid());
    EXPECT_TRUE(has_error(report, ValidationErrorCode::EmptyTopology));
}

TEST(TopologyValidationTest, ReportsDisconnectedNodesWithStructuredContext) {
    TopologyGraph graph;
    const RackId rack = graph.add_rack();
    const NicId nic = graph.add_nic(rack);
    static_cast<void>(graph.add_gpu(nic));
    const SwitchId disconnected_spine = graph.add_spine_switch();

    const ValidationReport report = validate_topology(graph);

    ASSERT_FALSE(report.valid());
    ASSERT_TRUE(has_error(report, ValidationErrorCode::DisconnectedNode));
    EXPECT_TRUE(
        std::ranges::any_of(report.errors, [disconnected_spine](const ValidationError& error) {
            return error.code == ValidationErrorCode::DisconnectedNode &&
                   error.node == NodeId{disconnected_spine};
        }));
}

} // namespace
} // namespace nexuslab::topology
