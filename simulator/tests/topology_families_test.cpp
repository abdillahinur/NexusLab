// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/export.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/topology/validation.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace nexuslab::topology {
namespace {

struct ExpectedCounts final {
    std::size_t racks;
    std::size_t gpus;
    std::size_t nics;
    std::size_t switches;
    std::size_t ports;
    std::size_t links;
};

void expect_counts(const TopologyGraph& graph, const ExpectedCounts& expected) {
    EXPECT_EQ(graph.racks().size(), expected.racks);
    EXPECT_EQ(graph.gpus().size(), expected.gpus);
    EXPECT_EQ(graph.nics().size(), expected.nics);
    EXPECT_EQ(graph.switches().size(), expected.switches);
    EXPECT_EQ(graph.ports().size(), expected.ports);
    EXPECT_EQ(graph.links().size(), expected.links);
}

void expect_leaf_spine_roles(const TopologyGraph& graph, std::size_t leaf_count) {
    for (std::size_t index = 0; index < graph.switches().size(); ++index) {
        const SwitchRole expected = index < leaf_count ? SwitchRole::Leaf : SwitchRole::Spine;
        EXPECT_EQ(graph.switches()[index].role, expected);
    }
}

void expect_invalid_leaf_spine(const LeafSpineConfig& config) {
    EXPECT_THROW(static_cast<void>(generate_leaf_spine(config)), std::invalid_argument);
}

TEST(TopologyFamiliesTest, GeneratesTwoGpuDirectTopology) {
    const std::unique_ptr<TopologyGraph> graph = generate_two_gpu_direct();
    ASSERT_NE(graph, nullptr);

    expect_counts(*graph, ExpectedCounts{2U, 2U, 2U, 0U, 6U, 3U});
    EXPECT_TRUE(validate_topology(*graph).valid());
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{1}}),
              (ShortestPathSummary{3U, 1U}));
    ASSERT_EQ(graph->racks().size(), 2U);
    EXPECT_EQ(graph->racks()[0].gpus, (std::vector{GpuId{0}}));
    EXPECT_EQ(graph->racks()[1].gpus, (std::vector{GpuId{1}}));
}

TEST(TopologyFamiliesTest, GeneratesConfigurableSingleRackTopology) {
    const std::unique_ptr<TopologyGraph> graph = generate_single_rack(SingleRackConfig{16U, 8U});
    ASSERT_NE(graph, nullptr);

    expect_counts(*graph, ExpectedCounts{1U, 16U, 2U, 1U, 36U, 18U});
    EXPECT_TRUE(validate_topology(*graph).valid());
    ASSERT_EQ(graph->switches().size(), 1U);
    EXPECT_EQ(graph->switches()[0].role, SwitchRole::Leaf);
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{8}}),
              (ShortestPathSummary{4U, 1U}));
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{1}}),
              (ShortestPathSummary{2U, 1U}));
}

TEST(TopologyFamiliesTest, GeneratesConfigurableLeafSpineTopology) {
    const LeafSpineConfig config{2U, 2U, 2U, 3U};
    const std::unique_ptr<TopologyGraph> graph = generate_leaf_spine(config);
    ASSERT_NE(graph, nullptr);

    expect_counts(*graph, ExpectedCounts{2U, 8U, 4U, 5U, 36U, 18U});
    EXPECT_TRUE(validate_topology(*graph).valid());
    EXPECT_EQ(graph->shortest_path_summary(NodeId{GpuId{0}}, NodeId{GpuId{4}}),
              (ShortestPathSummary{6U, 3U}));
    expect_leaf_spine_roles(*graph, config.leaf_count);
}

TEST(TopologyFamiliesTest, GenerationIsDeterministicForEveryFamily) {
    const std::array first{
        generate_two_gpu_direct(),
        generate_single_rack(SingleRackConfig{16U, 8U}),
        generate_leaf_spine(LeafSpineConfig{2U, 2U, 2U, 3U}),
    };
    const std::array second{
        generate_two_gpu_direct(),
        generate_single_rack(SingleRackConfig{16U, 8U}),
        generate_leaf_spine(LeafSpineConfig{2U, 2U, 2U, 3U}),
    };

    for (std::size_t index = 0; index < first.size(); ++index) {
        EXPECT_EQ(serialize_topology_yaml(*first[index]), serialize_topology_yaml(*second[index]));
    }
}

TEST(TopologyFamiliesTest, RejectsInvalidSingleRackConfigurations) {
    EXPECT_THROW(static_cast<void>(generate_single_rack(SingleRackConfig{0U, 8U})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(generate_single_rack(SingleRackConfig{8U, 0U})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(generate_single_rack(SingleRackConfig{9U, 8U})),
                 std::invalid_argument);
}

TEST(TopologyFamiliesTest, RejectsInvalidAndOverflowingLeafSpineConfigurations) {
    constexpr std::array invalid_configs{
        LeafSpineConfig{0U, 2U, 2U, 3U},
        LeafSpineConfig{2U, 0U, 2U, 3U},
        LeafSpineConfig{2U, 2U, 0U, 3U},
        LeafSpineConfig{2U, 2U, 2U, 0U},
    };
    for (const LeafSpineConfig& config : invalid_configs) {
        expect_invalid_leaf_spine(config);
    }

    EXPECT_THROW(static_cast<void>(generate_leaf_spine(
                     LeafSpineConfig{std::numeric_limits<std::size_t>::max(), 2U, 1U, 1U})),
                 std::overflow_error);
}

} // namespace
} // namespace nexuslab::topology
