// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/validation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>

namespace nexuslab::topology {
namespace {

void expect_graph_counts(const TopologyGraph& graph, const ClosDimensions& dimensions) {
    EXPECT_EQ(graph.gpus().size(), dimensions.gpu_count);
    EXPECT_EQ(graph.nics().size(), dimensions.nic_count);
    EXPECT_EQ(graph.racks().size(), dimensions.rack_count);
    EXPECT_EQ(graph.switches().size(), dimensions.switch_count);
    EXPECT_EQ(graph.ports().size(), dimensions.port_count);
    EXPECT_EQ(graph.links().size(), dimensions.link_count);
}

template <typename Entity>
void expect_equal_entities(std::span<const Entity> left, std::span<const Entity> right) {
    EXPECT_TRUE(std::ranges::equal(left, right));
}

void expect_rack_counts(const Rack& rack, const ClosConfig& config) {
    EXPECT_EQ(rack.nics.size(), config.nics_per_leaf);
    EXPECT_EQ(rack.gpus.size(), config.nics_per_leaf * config.gpus_per_nic);
}

void expect_rack_leaf(const TopologyGraph& graph, const Rack& rack, const ClosConfig& config) {
    ASSERT_EQ(rack.leaf_switches.size(), 1U);
    const Switch* leaf = graph.find(rack.leaf_switches.front());
    ASSERT_NE(leaf, nullptr);
    EXPECT_EQ(leaf->role, SwitchRole::Leaf);
    EXPECT_EQ(leaf->rack, rack.id);
    EXPECT_EQ(leaf->ports.size(), config.nics_per_leaf + config.spine_count);
}

void expect_all_rack_memberships(const TopologyGraph& graph, const ClosConfig& config) {
    for (const Rack& rack : graph.racks()) {
        expect_rack_counts(rack, config);
        expect_rack_leaf(graph, rack, config);
    }
}

void expect_spine_membership(const Switch& spine, const ClosDimensions& dimensions) {
    EXPECT_EQ(spine.role, SwitchRole::Spine);
    EXPECT_EQ(spine.rack, std::nullopt);
    EXPECT_EQ(spine.ports.size(), dimensions.leaf_count);
}

void expect_all_spine_memberships(const TopologyGraph& graph, const ClosDimensions& dimensions) {
    for (std::size_t index = dimensions.leaf_count; index < graph.switches().size(); ++index) {
        expect_spine_membership(graph.switches()[index], dimensions);
    }
}

void expect_invalid_config(const ClosConfig& config) {
    EXPECT_THROW(static_cast<void>(generate_clos(config)), std::invalid_argument);
}

TEST(ClosTopologyTest, ApprovedProfilesHaveExpectedDimensions) {
    EXPECT_EQ(clos_dimensions(initial_clos_config()),
              (ClosDimensions{512U, 64U, 8U, 8U, 8U, 16U, 1'280U, 640U}));
    EXPECT_EQ(clos_dimensions(stretch_clos_config()),
              (ClosDimensions{2'048U, 256U, 32U, 8U, 32U, 40U, 5'120U, 2'560U}));
}

TEST(ClosTopologyTest, GeneratesAndValidatesInitialAndStretchTargets) {
    const std::array configs{initial_clos_config(), stretch_clos_config()};

    for (const ClosConfig& config : configs) {
        const ClosDimensions dimensions = clos_dimensions(config);
        const std::unique_ptr<TopologyGraph> graph = generate_clos(config);
        ASSERT_NE(graph, nullptr);
        expect_graph_counts(*graph, dimensions);
        EXPECT_TRUE(validate_topology(*graph).valid());
    }
}

TEST(ClosTopologyTest, AssignsCanonicalRackLeafNicAndGpuMembership) {
    const ClosConfig config = initial_clos_config();
    const std::unique_ptr<TopologyGraph> graph = generate_clos(config);

    ASSERT_NE(graph, nullptr);
    const ClosDimensions dimensions = clos_dimensions(config);
    expect_all_rack_memberships(*graph, config);
    expect_all_spine_memberships(*graph, dimensions);
}

TEST(ClosTopologyTest, GenerationIsDeterministicAcrossIndependentGraphs) {
    const ClosConfig config{64U, 8U, 8U, 8U};
    const std::unique_ptr<TopologyGraph> first = generate_clos(config);
    const std::unique_ptr<TopologyGraph> second = generate_clos(config);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    expect_equal_entities(first->gpus(), second->gpus());
    expect_equal_entities(first->nics(), second->nics());
    expect_equal_entities(first->switches(), second->switches());
    expect_equal_entities(first->racks(), second->racks());
    expect_equal_entities(first->ports(), second->ports());
    expect_equal_entities(first->links(), second->links());
    for (const Switch& switch_entity : first->switches()) {
        EXPECT_TRUE(std::ranges::equal(first->outgoing(NodeId{switch_entity.id}),
                                       second->outgoing(NodeId{switch_entity.id})));
    }
}

TEST(ClosTopologyTest, RejectsZeroAndPartiallyPopulatedConfigurations) {
    expect_invalid_config(ClosConfig{0U, 8U, 8U, 8U});
    expect_invalid_config(ClosConfig{64U, 0U, 8U, 8U});
    expect_invalid_config(ClosConfig{64U, 8U, 0U, 8U});
    expect_invalid_config(ClosConfig{64U, 8U, 8U, 0U});
    expect_invalid_config(ClosConfig{65U, 8U, 8U, 8U});
    expect_invalid_config(ClosConfig{72U, 8U, 8U, 8U});
}

TEST(ClosTopologyTest, RejectsDerivedEntityCountOverflowBeforeAllocation) {
    const ClosConfig config{std::numeric_limits<std::size_t>::max(), 1U, 1U,
                            std::numeric_limits<std::size_t>::max()};

    EXPECT_THROW(static_cast<void>(generate_clos(config)), std::overflow_error);
}

TEST(ClosTopologyTest, ReportsKnownEqualCostPathCounts) {
    const std::unique_ptr<TopologyGraph> graph = generate_clos(initial_clos_config());
    ASSERT_NE(graph, nullptr);

    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{64}}),
              (ShortestPathSummary{6U, 8U}));
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{8}}),
              (ShortestPathSummary{4U, 1U}));
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{1}}),
              (ShortestPathSummary{2U, 1U}));
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{0}}),
              (ShortestPathSummary{0U, 1U}));
}

TEST(ClosTopologyTest, SpineLinkFailureRemovesExactlyOneEqualCostPath) {
    const ClosConfig config = initial_clos_config();
    const ClosDimensions dimensions = clos_dimensions(config);
    const std::unique_ptr<TopologyGraph> graph = generate_clos(config);
    ASSERT_NE(graph, nullptr);
    const std::size_t first_leaf_spine_link = dimensions.gpu_count + dimensions.nic_count;

    ASSERT_TRUE(graph->set_link_state(LinkId{first_leaf_spine_link}, OperationalState::Down));
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{64}}),
              (ShortestPathSummary{6U, 7U}));

    ASSERT_TRUE(graph->set_link_state(LinkId{first_leaf_spine_link}, OperationalState::Up));
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{64}}),
              (ShortestPathSummary{6U, 8U}));
}

} // namespace
} // namespace nexuslab::topology
