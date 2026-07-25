// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/id.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace nexuslab::topology {
namespace {

TEST(TopologyIdTest, EntityKindsAreDistinctStrongTypes) {
    static_assert(!std::is_same_v<GpuId, NicId>);
    static_assert(!std::is_convertible_v<GpuId, NicId>);
    constexpr GpuId first{10};
    constexpr GpuId second{11};

    EXPECT_LT(first, second);
    EXPECT_EQ(first.value(), 10U);
}

TEST(TopologyIdTest, PerKindGeneratorsAssignIndependentDeterministicIds) {
    SequentialIdGenerator<GpuId> gpu_ids;
    SequentialIdGenerator<NicId> nic_ids;

    const auto generated =
        std::tuple{gpu_ids.next(), gpu_ids.next(), nic_ids.next(), nic_ids.next()};

    EXPECT_EQ(generated, (std::tuple{GpuId{0}, GpuId{1}, NicId{0}, NicId{1}}));
}

TEST(TopologyIdTest, GenericNodeIdUsesExplicitStableKindTag) {
    constexpr NodeId gpu{GpuId{7}};
    constexpr NodeId nic{NicId{7}};
    constexpr NodeId network_switch{SwitchId{7}};

    EXPECT_EQ((std::tuple{gpu.kind(), gpu.value(), nic.kind(), nic.value(), network_switch.kind(),
                          network_switch.value()}),
              (std::tuple{NodeKind::Gpu, 7U, NodeKind::Nic, 7U, NodeKind::Switch, 7U}));
    EXPECT_NE(gpu, nic);
}

TEST(TopologyIdTest, GeneratorRejectsSequenceOverflow) {
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    SequentialIdGenerator<LinkId> link_ids{maximum};

    EXPECT_EQ(link_ids.next(), LinkId{maximum});
    EXPECT_THROW(static_cast<void>(link_ids.next()), std::overflow_error);
}

TEST(TopologyEntityTest, ValueEntitiesPreserveExplicitRelationships) {
    const GpuWorker gpu{GpuId{0}, RackId{0}, NicId{0}, PortId{0}};
    const Nic nic{NicId{0}, RackId{0}, {gpu.id}, {PortId{1}, PortId{2}}};
    const Switch leaf{
        SwitchId{0}, RackId{0}, SwitchRole::Leaf, {PortId{3}, PortId{4}}, OperationalState::Up};
    const Switch spine{
        SwitchId{1}, std::nullopt, SwitchRole::Spine, {PortId{5}}, OperationalState::Up};
    const Rack rack{RackId{0}, {gpu.id}, {nic.id}, {leaf.id}};

    EXPECT_EQ((std::tuple{gpu.attached_nic, nic.attached_gpus, leaf.rack, spine.rack,
                          rack.leaf_switches}),
              (std::tuple{nic.id, std::vector{gpu.id}, std::optional{rack.id},
                          std::optional<RackId>{}, std::vector{leaf.id}}));
}

TEST(TopologyEntityTest, PhysicalLinkProducesBothDirectedArcs) {
    const PhysicalLink link{LinkId{7}, PortId{10}, PortId{20}, LinkKind::Fabric,
                            OperationalState::Up};

    const auto arcs = directed_links(link);

    EXPECT_EQ(arcs[0],
              (DirectedLink{DirectedLinkId{link.id, LinkDirection::AToB}, PortId{10}, PortId{20}}));
    EXPECT_EQ(arcs[1],
              (DirectedLink{DirectedLinkId{link.id, LinkDirection::BToA}, PortId{20}, PortId{10}}));
}

TEST(TopologyEntityTest, OperationalStateChangesDoNotChangeIdentityOrEndpoints) {
    PhysicalLink link{LinkId{7}, PortId{10}, PortId{20}, LinkKind::Fabric, OperationalState::Up};
    const auto identity_before = std::tuple{link.id, link.endpoint_a, link.endpoint_b, link.kind};

    link.state = OperationalState::Down;

    EXPECT_EQ((std::tuple{link.id, link.endpoint_a, link.endpoint_b, link.kind}), identity_before);
    EXPECT_EQ(link.state, OperationalState::Down);
}

} // namespace
} // namespace nexuslab::topology
