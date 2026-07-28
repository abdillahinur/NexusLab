// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/transport/runtime.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nexuslab::transport {
namespace {

struct TwoHopFabric final {
    topology::DirectedLinkId first;
    topology::DirectedLinkId second;
};

[[nodiscard]] TwoHopFabric populate_two_hop_fabric(topology::TopologyGraph& graph) {
    const topology::RackId rack = graph.add_rack();
    const topology::NicId nic = graph.add_nic(rack);
    const topology::SwitchId leaf = graph.add_leaf_switch(rack);
    const topology::SwitchId spine = graph.add_spine_switch();
    const topology::LinkId first =
        graph.connect_fabric(topology::NodeId{nic}, topology::PortRole::FabricUplink,
                             topology::NodeId{leaf}, topology::PortRole::FabricDownlink);
    const topology::LinkId second =
        graph.connect_fabric(topology::NodeId{leaf}, topology::PortRole::FabricUplink,
                             topology::NodeId{spine}, topology::PortRole::FabricDownlink);
    return TwoHopFabric{
        topology::DirectedLinkId{first, topology::LinkDirection::AToB},
        topology::DirectedLinkId{second, topology::LinkDirection::AToB},
    };
}

[[nodiscard]] DirectedLinkConfiguration runtime_configuration(topology::DirectedLinkId link) {
    return DirectedLinkConfiguration{
        link,         BitsPerSecond{8'000'000'000ULL}, sim::SimDurationNs{25}, ByteCount{1'000},
        std::nullopt,
    };
}

[[nodiscard]] RoutedChunk routed_chunk(ChunkId id, const TwoHopFabric& fabric) {
    return RoutedChunk{
        TransferChunk{TransferId{7}, id, ByteCount{100}, 0, false},
        {fabric.first, fabric.second},
    };
}

[[nodiscard]] RoutedChunk one_hop_chunk(ChunkId id, topology::DirectedLinkId link) {
    return RoutedChunk{
        TransferChunk{TransferId{7}, id, ByteCount{100}, 0, false},
        {link},
    };
}

class RuntimeDispatcher final {
  public:
    struct StateChange final {
        topology::LinkId link;
        topology::OperationalState state;
        sim::SimTimeNs timestamp;
    };

    RuntimeDispatcher(TransportRuntime& runtime, std::vector<ChunkId> starts,
                      std::vector<StateChange> state_changes = {},
                      std::vector<ChunkId> recovery_starts = {})
        : runtime_{&runtime}, starts_{std::move(starts)}, state_changes_{std::move(state_changes)},
          recovery_starts_{std::move(recovery_starts)} {}

    void operator()(const sim::NoOpEvent& event, sim::SimulationContext& context) {
        static_cast<void>(event);
        for (const ChunkId chunk : starts_) {
            static_cast<void>(runtime_->schedule_initial_arrival(chunk, context));
        }
        for (const StateChange& change : state_changes_) {
            static_cast<void>(runtime_->schedule_link_state_change(change.link, change.state,
                                                                   change.timestamp, context));
        }
    }

    void operator()(const ChunkArrivalEvent& event, sim::SimulationContext& context) {
        runtime_->handle_arrival(event, context);
        const auto snapshot = runtime_->chunk_snapshot(event.chunk);
        if (snapshot.has_value() && snapshot->state == ChunkTransitState::Delivered) {
            deliveries_.emplace_back(event.chunk, context.now());
        }
    }

    void operator()(const TransmissionCompleteEvent& event, sim::SimulationContext& context) {
        runtime_->handle_completion(event, context);
    }

    void operator()(const LinkStateChangeEvent& event, sim::SimulationContext& context) {
        runtime_->handle_link_state_change(event, context);
        if (event.state == topology::OperationalState::Up) {
            for (const ChunkId chunk : recovery_starts_) {
                static_cast<void>(runtime_->schedule_initial_arrival(chunk, context));
            }
        }
    }

    [[nodiscard]] const std::vector<std::pair<ChunkId, sim::SimTimeNs>>&
    deliveries() const noexcept {
        return deliveries_;
    }

  private:
    TransportRuntime* runtime_;
    std::vector<ChunkId> starts_;
    std::vector<StateChange> state_changes_;
    std::vector<ChunkId> recovery_starts_;
    std::vector<std::pair<ChunkId, sim::SimTimeNs>> deliveries_;
};

[[nodiscard]] std::size_t count_scheduled_kind(std::span<const sim::TraceRecord> records,
                                               sim::EventPayloadKind kind,
                                               sim::EventPriority expected_priority) {
    std::size_t count{0};
    for (const sim::TraceRecord& record : records) {
        if (record.action == sim::TraceAction::Scheduled && record.payload_kind == kind) {
            EXPECT_EQ(record.priority, expected_priority);
            ++count;
        }
    }
    return count;
}

TEST(TransportRuntimeTest, RejectsUnknownUnconfiguredAndNoncontiguousRoutes) {
    topology::TopologyGraph graph;
    const TwoHopFabric fabric = populate_two_hop_fabric(graph);
    TransportRuntime runtime{
        graph,
        {runtime_configuration(fabric.first), runtime_configuration(fabric.second)},
    };

    EXPECT_THROW(
        TransportRuntime(graph, {runtime_configuration(topology::DirectedLinkId{
                                    topology::LinkId{99}, topology::LinkDirection::AToB})}),
        std::invalid_argument);
    EXPECT_THROW(runtime.register_chunk(
                     RoutedChunk{TransferChunk{TransferId{7}, ChunkId{0}, ByteCount{100}, 0, false},
                                 {fabric.second, fabric.first}}),
                 std::invalid_argument);

    topology::TopologyGraph disconnected_graph;
    const TwoHopFabric disconnected = populate_two_hop_fabric(disconnected_graph);
    TransportRuntime incomplete_runtime{
        disconnected_graph,
        {runtime_configuration(disconnected.first)},
    };
    EXPECT_THROW(incomplete_runtime.register_chunk(routed_chunk(ChunkId{0}, disconnected)),
                 std::invalid_argument);
}

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(TransportRuntimeTest, DeliversTwoHopChunkAtExactAnalyticalTime) {
    topology::TopologyGraph graph;
    const TwoHopFabric fabric = populate_two_hop_fabric(graph);
    TransportRuntime runtime{
        graph,
        {runtime_configuration(fabric.first), runtime_configuration(fabric.second)},
    };
    runtime.register_chunk(routed_chunk(ChunkId{0}, fabric));
    RuntimeDispatcher dispatcher{runtime, {ChunkId{0}}};
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, sim::SimTimeNs{260});
    EXPECT_EQ(result.dispatched_events, 6U);
    EXPECT_EQ(dispatcher.deliveries(), (std::vector{std::pair{ChunkId{0}, sim::SimTimeNs{260}}}));
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{0}),
              (ChunkTransitSnapshot{ChunkTransitState::Delivered, 2, false}));
    ASSERT_NE(runtime.find_service(fabric.first), nullptr);
    ASSERT_NE(runtime.find_service(fabric.second), nullptr);
    EXPECT_FALSE(runtime.find_service(fabric.first)->queue().snapshot().busy);
    EXPECT_FALSE(runtime.find_service(fabric.second)->queue().snapshot().busy);
    EXPECT_EQ(count_scheduled_kind(simulation.trace_records(),
                                   sim::EventPayloadKind::TransmissionComplete,
                                   sim::EventPriority::Control),
              2U);
    EXPECT_EQ(count_scheduled_kind(simulation.trace_records(), sim::EventPayloadKind::ChunkArrival,
                                   sim::EventPriority::Normal),
              3U);
}

TEST(TransportRuntimeTest, PropagationOverlapsFollowingSerialization) {
    topology::TopologyGraph graph;
    const TwoHopFabric fabric = populate_two_hop_fabric(graph);
    TransportRuntime runtime{
        graph,
        {runtime_configuration(fabric.first), runtime_configuration(fabric.second)},
    };
    runtime.register_chunk(routed_chunk(ChunkId{0}, fabric));
    runtime.register_chunk(routed_chunk(ChunkId{1}, fabric));
    RuntimeDispatcher dispatcher{runtime, {ChunkId{0}, ChunkId{1}}};
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, sim::SimTimeNs{360});
    EXPECT_EQ(dispatcher.deliveries(), (std::vector{
                                           std::pair{ChunkId{0}, sim::SimTimeNs{260}},
                                           std::pair{ChunkId{1}, sim::SimTimeNs{360}},
                                       }));
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{0}),
              (ChunkTransitSnapshot{ChunkTransitState::Delivered, 2, false}));
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{1}),
              (ChunkTransitSnapshot{ChunkTransitState::Delivered, 2, false}));
}

TEST(TransportRuntimeTest, DropsAtArrivalWhenRemainingLinkIsDown) {
    topology::TopologyGraph graph;
    const TwoHopFabric fabric = populate_two_hop_fabric(graph);
    TransportRuntime runtime{
        graph,
        {runtime_configuration(fabric.first), runtime_configuration(fabric.second)},
    };
    runtime.register_chunk(routed_chunk(ChunkId{0}, fabric));
    ASSERT_TRUE(graph.set_link_state(fabric.second.link, topology::OperationalState::Down));
    RuntimeDispatcher dispatcher{runtime, {ChunkId{0}}};
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, sim::SimTimeNs{135});
    EXPECT_TRUE(dispatcher.deliveries().empty());
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{0}),
              (ChunkTransitSnapshot{ChunkTransitState::DroppedLinkDown, 1, false}));
}

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(TransportRuntimeTest, FailureDrainsBothDirectionsAndRecoveryStartsEmpty) {
    topology::TopologyGraph graph;
    const TwoHopFabric fabric = populate_two_hop_fabric(graph);
    const topology::DirectedLinkId reverse{
        fabric.first.link,
        topology::LinkDirection::BToA,
    };
    TransportRuntime runtime{
        graph,
        {runtime_configuration(fabric.first), runtime_configuration(reverse)},
    };
    runtime.register_chunk(one_hop_chunk(ChunkId{0}, fabric.first));
    runtime.register_chunk(one_hop_chunk(ChunkId{1}, fabric.first));
    runtime.register_chunk(one_hop_chunk(ChunkId{2}, fabric.first));
    runtime.register_chunk(one_hop_chunk(ChunkId{3}, reverse));
    RuntimeDispatcher dispatcher{
        runtime,
        {ChunkId{0}, ChunkId{1}, ChunkId{3}},
        {
            {fabric.first.link, topology::OperationalState::Down, sim::SimTimeNs{110}},
            {fabric.first.link, topology::OperationalState::Up, sim::SimTimeNs{120}},
        },
        {ChunkId{2}},
    };
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, sim::SimTimeNs{245});
    EXPECT_EQ(result.cancelled_events, 2U);
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{0}),
              (ChunkTransitSnapshot{ChunkTransitState::DroppedLinkDown, 0, false}));
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{1}),
              (ChunkTransitSnapshot{ChunkTransitState::DroppedLinkDown, 0, false}));
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{2}),
              (ChunkTransitSnapshot{ChunkTransitState::Delivered, 1, false}));
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{3}),
              (ChunkTransitSnapshot{ChunkTransitState::DroppedLinkDown, 0, false}));
    EXPECT_EQ(dispatcher.deliveries(), (std::vector{std::pair{ChunkId{2}, sim::SimTimeNs{245}}}));
    ASSERT_NE(runtime.find_service(fabric.first), nullptr);
    EXPECT_EQ(runtime.find_service(fabric.first)->queue().snapshot(),
              (QueueSnapshot{ByteCount{0}, 0, ByteCount{100}, 1, false}));
    EXPECT_FALSE(runtime.find_service(fabric.first)->scheduled_completion().has_value());
    ASSERT_NE(runtime.find_service(reverse), nullptr);
    EXPECT_EQ(runtime.find_service(reverse)->queue().snapshot(),
              (QueueSnapshot{ByteCount{0}, 0, ByteCount{0}, 0, false}));
    EXPECT_FALSE(runtime.find_service(reverse)->scheduled_completion().has_value());
    EXPECT_EQ(count_scheduled_kind(simulation.trace_records(),
                                   sim::EventPayloadKind::LinkStateChange,
                                   sim::EventPriority::Critical),
              2U);
}

TEST(TransportRuntimeTest, FailureDoesNotRecallChunkAlreadyPropagating) {
    topology::TopologyGraph graph;
    const TwoHopFabric fabric = populate_two_hop_fabric(graph);
    TransportRuntime runtime{
        graph,
        {runtime_configuration(fabric.first), runtime_configuration(fabric.second)},
    };
    runtime.register_chunk(routed_chunk(ChunkId{0}, fabric));
    RuntimeDispatcher dispatcher{
        runtime,
        {ChunkId{0}},
        {{fabric.first.link, topology::OperationalState::Down, sim::SimTimeNs{120}}},
    };
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, sim::SimTimeNs{260});
    EXPECT_EQ(result.cancelled_events, 0U);
    EXPECT_EQ(runtime.chunk_snapshot(ChunkId{0}),
              (ChunkTransitSnapshot{ChunkTransitState::Delivered, 2, false}));
    EXPECT_EQ(dispatcher.deliveries(), (std::vector{std::pair{ChunkId{0}, sim::SimTimeNs{260}}}));
}

} // namespace
} // namespace nexuslab::transport
