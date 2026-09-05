// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <source_location>
#include <utility>
#include <vector>

namespace nexuslab::transport {
namespace {

template <typename Actual, typename Expected>
void expect_equal(const Actual& actual, const Expected& expected,
                  std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_EQ(actual, expected);
}

template <typename Exception, typename Operation>
[[nodiscard]] bool throws_as(Operation operation) {
    try {
        operation();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

struct Fabric final {
    topology::TopologyGraph graph;
    topology::NicId source;
    topology::SwitchId leaf;
    topology::SwitchId spine;
    topology::DirectedLinkId first;
    topology::DirectedLinkId second;

    Fabric()
        : source{graph.add_nic(graph.add_rack())}, leaf{graph.add_leaf_switch(topology::RackId{0})},
          spine{graph.add_spine_switch()},
          first{graph.connect_fabric(topology::NodeId{source}, topology::PortRole::FabricUplink,
                                     topology::NodeId{leaf}, topology::PortRole::FabricDownlink),
                topology::LinkDirection::AToB},
          second{graph.connect_fabric(topology::NodeId{leaf}, topology::PortRole::FabricUplink,
                                      topology::NodeId{spine}, topology::PortRole::FabricDownlink),
                 topology::LinkDirection::AToB} {}

    [[nodiscard]] std::vector<DirectedLinkConfiguration>
    config(std::uint64_t capacity = 1000) const {
        return {{first, BitsPerSecond{8'000'000'000ULL}, sim::SimDurationNs{25},
                 ByteCount{capacity}, ByteCount{100}},
                {second, BitsPerSecond{8'000'000'000ULL}, sim::SimDurationNs{25},
                 ByteCount{capacity}, ByteCount{100}}};
    }
    [[nodiscard]] TransferRequest request(std::uint64_t bytes = 250) const {
        return {topology::NodeId{source},
                topology::NodeId{spine},
                ByteCount{bytes},
                ByteCount{100},
                {first, second}};
    }
};

template <typename Value> [[nodiscard]] Value require_value(const std::optional<Value>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"expected test value is absent"};
    }
    return *value;
}

[[nodiscard]] TrafficCount accounted(const TransferSnapshot& snapshot) {
    TrafficCount total{};
    for (const auto count :
         {snapshot.registered, snapshot.awaiting_initial_arrival, snapshot.waiting, snapshot.active,
          snapshot.propagating, snapshot.delivered, snapshot.dropped_buffer_full,
          snapshot.dropped_link_down}) {
        total = add_traffic(total, count);
    }
    return total;
}

class ProgressDispatcher final {
  public:
    explicit ProgressDispatcher(TransportRuntime& runtime) : runtime_{runtime} {}
    std::function<void(sim::SimulationContext&)> bootstrap;
    std::function<void()> observe;
    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        bootstrap(context);
        inspect();
    }
    void operator()(const ChunkArrivalEvent& event, sim::SimulationContext& context) {
        runtime_.handle_arrival(event, context);
        inspect();
    }
    void operator()(const TransmissionCompleteEvent& event, sim::SimulationContext& context) {
        runtime_.handle_completion(event, context);
        inspect();
    }
    void operator()(const LinkStateChangeEvent& event, sim::SimulationContext& context) {
        runtime_.handle_link_state_change(event, context);
        inspect();
    }
    void operator()(const PortStateChangeEvent& event, sim::SimulationContext& context) {
        runtime_.handle_port_state_change(event, context);
        inspect();
    }
    void operator()(const SwitchStateChangeEvent& event, sim::SimulationContext& context) {
        runtime_.handle_switch_state_change(event, context);
        inspect();
    }

  private:
    void inspect() const {
        if (observe) {
            observe();
        }
    }
    TransportRuntime& runtime_;
};

[[nodiscard]] sim::SimulationResult execute(ProgressDispatcher& dispatcher,
                                            sim::SimTimeNs start = sim::SimTimeNs{0}) {
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule({start, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    return simulation.run(dispatcher);
}

TEST(TransportProgressTest, ConservesBytesAtEveryEventAndEmitsCompletionOnce) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.submit_transfer(fabric.request(), context));
    };
    dispatcher.observe = [&] {
        const auto snapshot = runtime.transfer_snapshot(TransferId{0});
        ASSERT_TRUE(snapshot.has_value());
        expect_equal(accounted(require_value(snapshot)), (TrafficCount{250, 3}));
    };
    const auto result = execute(dispatcher);
    expect_equal(result.status, sim::SimulationStatus::Completed);
    expect_equal(result.final_time, sim::SimTimeNs{400});
    const auto completions = runtime.take_completed_transfers();
    ASSERT_EQ(completions.size(), 1U);
    expect_equal(completions.front(), (TransferCompletion{TransferId{0},
                                                          TransferOutcome::Succeeded,
                                                          sim::SimTimeNs{400},
                                                          TrafficCount{250, 3},
                                                          {},
                                                          {}}));
    EXPECT_TRUE(runtime.take_completed_transfers().empty());
    expect_equal(require_value(runtime.transfer_snapshot(TransferId{0})).completion,
                 completions.front());
}

TEST(TransportProgressTest, LossWaitsForOtherChunksAndPreservesDropReason) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config(100)};
    ProgressDispatcher dispatcher{runtime};
    bool saw_pending_loss{false};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.submit_transfer(fabric.request(), context));
    };
    dispatcher.observe = [&] {
        const auto snapshot = require_value(runtime.transfer_snapshot(TransferId{0}));
        expect_equal(accounted(snapshot), snapshot.total);
        saw_pending_loss = saw_pending_loss || (snapshot.dropped_buffer_full.bytes == 50 &&
                                                !snapshot.completion.has_value());
    };
    expect_equal(execute(dispatcher).status, sim::SimulationStatus::Completed);
    EXPECT_TRUE(saw_pending_loss);
    const auto completion = runtime.take_completed_transfers();
    ASSERT_EQ(completion.size(), 1U);
    expect_equal(completion.front().outcome, TransferOutcome::Failed);
    expect_equal(completion.front().delivered, (TrafficCount{200, 2}));
    expect_equal(completion.front().dropped_buffer_full, (TrafficCount{50, 1}));
}

TEST(TransportProgressTest, CountsPerHopMarksDropsAndBusyTime) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config(100)};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.submit_transfer(fabric.request(), context));
    };
    const auto result = execute(dispatcher);
    const auto first = runtime.find_service(fabric.first)->statistics(result.final_time);
    expect_equal(first.enqueued, (TrafficCount{200, 2}));
    expect_equal(first.started, first.enqueued);
    expect_equal(first.completed, first.enqueued);
    expect_equal(first.marked, (TrafficCount{100, 1}));
    expect_equal(first.dropped_buffer_full, (TrafficCount{50, 1}));
    expect_equal(first.busy_time, sim::SimDurationNs{200});
    expect_equal(runtime.find_service(fabric.second)->statistics(result.final_time).marked,
                 TrafficCount{});
}

TEST(TransportProgressTest, PortAndSwitchFailureDrainActiveAndQueuedWork) {
    for (bool port_failure : {false, true}) {
        Fabric fabric;
        TransportRuntime runtime{fabric.graph, fabric.config()};
        ProgressDispatcher dispatcher{runtime};
        dispatcher.bootstrap = [&](sim::SimulationContext& context) {
            static_cast<void>(runtime.submit_transfer(fabric.request(), context));
            if (port_failure) {
                static_cast<void>(runtime.schedule_port_state_change(
                    fabric.graph.find(fabric.first.link)->endpoint_a,
                    topology::OperationalState::Down, sim::SimTimeNs{50}, context));
            } else {
                static_cast<void>(runtime.schedule_switch_state_change(
                    fabric.leaf, topology::OperationalState::Down, sim::SimTimeNs{50}, context));
            }
        };
        const auto result = execute(dispatcher);
        expect_equal(result.cancelled_events, 1U);
        const auto snapshot = require_value(runtime.transfer_snapshot(TransferId{0}));
        expect_equal(snapshot.dropped_link_down, snapshot.total);
        ASSERT_TRUE(snapshot.completion.has_value());
        expect_equal(require_value(snapshot.completion).timestamp, sim::SimTimeNs{50});
        expect_equal(runtime.find_service(fabric.first)->statistics(result.final_time).busy_time,
                     sim::SimDurationNs{50});
        expect_equal(runtime.find_service(fabric.first)->statistics(result.final_time).completed,
                     TrafficCount{});
    }
}

TEST(TransportProgressTest, OverlappingFailuresDoNotRestoreUnavailableArc) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.submit_transfer(fabric.request(), context));
        static_cast<void>(runtime.schedule_switch_state_change(
            fabric.leaf, topology::OperationalState::Down, sim::SimTimeNs{50}, context));
        static_cast<void>(runtime.schedule_link_state_change(
            fabric.first.link, topology::OperationalState::Down, sim::SimTimeNs{60}, context));
        static_cast<void>(runtime.schedule_link_state_change(
            fabric.first.link, topology::OperationalState::Up, sim::SimTimeNs{70}, context));
    };
    const auto result = execute(dispatcher);
    expect_equal(result.cancelled_events, 1U);
    expect_equal(
        runtime.find_service(fabric.first)->statistics(result.final_time).dropped_link_down,
        (TrafficCount{250, 3}));
    EXPECT_FALSE(fabric.graph.is_operational(
        topology::directed_links(*fabric.graph.find(fabric.first.link))[0]));
    expect_equal(runtime.take_completed_transfers().size(), 1U);
}

TEST(TransportProgressTest, SealsManualMembershipAndAccountsUnscheduledChunks) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    const RoutedChunk first{{TransferId{9}, ChunkId{2}, ByteCount{100}, 0, false}, {fabric.first}};
    auto second = first;
    second.chunk.id = ChunkId{3};
    runtime.register_chunk(first);
    runtime.register_chunk(second);
    expect_equal(require_value(runtime.transfer_snapshot(TransferId{9})).registered,
                 (TrafficCount{200, 2}));
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.schedule_initial_arrival(ChunkId{2}, context));
        auto late = first;
        late.chunk.id = ChunkId{4};
        EXPECT_TRUE(throws_as<std::invalid_argument>([&] { runtime.register_chunk(late); }));
    };
    expect_equal(execute(dispatcher).status, sim::SimulationStatus::Completed);
    const auto snapshot = require_value(runtime.transfer_snapshot(TransferId{9}));
    expect_equal(snapshot.delivered, (TrafficCount{100, 1}));
    expect_equal(snapshot.registered, (TrafficCount{100, 1}));
    EXPECT_FALSE(snapshot.completion.has_value());
    EXPECT_TRUE(runtime.take_completed_transfers().empty());
}

TEST(TransportProgressTest, RejectsLimitsAndTimingBeforeConsumingTransferIds) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config(), TransportLimits{3, 6, 2}};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        EXPECT_TRUE(throws_as<std::length_error>(
            [&] { static_cast<void>(runtime.submit_transfer(fabric.request(400), context)); }));
        auto too_long = fabric.request(100);
        too_long.route.push_back(fabric.first);
        EXPECT_TRUE(throws_as<std::length_error>(
            [&] { static_cast<void>(runtime.submit_transfer(too_long, context)); }));
        const auto accepted = runtime.submit_transfer(fabric.request(), context);
        expect_equal(accepted.id, TransferId{0});
    };
    expect_equal(execute(dispatcher).status, sim::SimulationStatus::Completed);
    EXPECT_FALSE(runtime.transfer_snapshot(TransferId{1}).has_value());
}

TEST(TransportProgressTest, RejectsLateSubmissionBeforeAnyMutation) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        EXPECT_TRUE(throws_as<std::overflow_error>(
            [&] { static_cast<void>(runtime.submit_transfer(fabric.request(), context)); }));
        EXPECT_FALSE(runtime.transfer_snapshot(TransferId{0}).has_value());
        EXPECT_FALSE(runtime.chunk_snapshot(ChunkId{0}).has_value());
    };
    expect_equal(
        execute(dispatcher, sim::SimTimeNs{std::numeric_limits<std::uint64_t>::max()}).status,
        sim::SimulationStatus::Completed);
}

TEST(TransportProgressTest, OutcomesAreOrderedByTerminalEventRatherThanSubmission) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.submit_transfer(fabric.request(), context));
        const TransferRequest short_transfer{topology::NodeId{fabric.leaf},
                                             topology::NodeId{fabric.spine},
                                             ByteCount{1},
                                             ByteCount{1},
                                             {fabric.second}};
        static_cast<void>(runtime.submit_transfer(short_transfer, context));
    };
    expect_equal(execute(dispatcher).status, sim::SimulationStatus::Completed);
    const auto completion = runtime.take_completed_transfers();
    ASSERT_EQ(completion.size(), 2U);
    expect_equal(completion[0].transfer, TransferId{1});
    expect_equal(completion[1].transfer, TransferId{0});
}

TEST(TransportProgressTest, RepeatsDomainOutcomesAndCountersWithFailure) {
    std::optional<TransferSnapshot> expected;
    std::optional<LinkStatistics> expected_statistics;
    for (int repetition = 0; repetition < 10; ++repetition) {
        Fabric fabric;
        TransportRuntime runtime{fabric.graph, fabric.config()};
        ProgressDispatcher dispatcher{runtime};
        dispatcher.bootstrap = [&](sim::SimulationContext& context) {
            static_cast<void>(runtime.submit_transfer(fabric.request(), context));
            static_cast<void>(runtime.schedule_link_state_change(fabric.second.link,
                                                                 topology::OperationalState::Down,
                                                                 sim::SimTimeNs{150}, context));
        };
        const auto result = execute(dispatcher);
        const auto snapshot = runtime.transfer_snapshot(TransferId{0});
        const auto statistics = runtime.find_service(fabric.second)->statistics(result.final_time);
        if (!expected.has_value()) {
            expected = snapshot;
            expected_statistics = statistics;
        }
        expect_equal(snapshot, expected);
        expect_equal(statistics, expected_statistics);
        expect_equal(accounted(require_value(snapshot)), require_value(snapshot).total);
    }
}

TEST(TransportProgressTest, RejectsCounterOverflowAndUnknownFailureTargets) {
    EXPECT_TRUE(throws_as<std::overflow_error>([&] {
        static_cast<void>(add_traffic(TrafficCount{std::numeric_limits<std::uint64_t>::max(), 1},
                                      TrafficCount{1, 1}));
    }));
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        EXPECT_TRUE(throws_as<std::invalid_argument>([&] {
            static_cast<void>(runtime.schedule_port_state_change(
                topology::PortId{999}, topology::OperationalState::Down, context.now(), context));
        }));
        EXPECT_TRUE(throws_as<std::invalid_argument>([&] {
            static_cast<void>(runtime.schedule_switch_state_change(
                topology::SwitchId{999}, topology::OperationalState::Down, context.now(), context));
        }));
    };
    expect_equal(execute(dispatcher).status, sim::SimulationStatus::Completed);
}

TEST(TransportProgressTest, StoppedRunPreservesActiveProgressAndLiveBusyTime) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    int bootstraps{0};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        if (bootstraps++ == 0) {
            static_cast<void>(runtime.submit_transfer(fabric.request(), context));
            static_cast<void>(context.schedule(
                {sim::SimTimeNs{50}, sim::EventPriority::Background, sim::NoOpEvent{1}}));
        } else {
            context.stop(sim::StopReason::Requested);
        }
    };
    const auto result = execute(dispatcher);
    expect_equal(result.status, sim::SimulationStatus::Stopped);
    const auto snapshot = require_value(runtime.transfer_snapshot(TransferId{0}));
    expect_equal(snapshot.active, (TrafficCount{100, 1}));
    expect_equal(snapshot.waiting, (TrafficCount{150, 2}));
    EXPECT_FALSE(snapshot.completion.has_value());
    const auto* service = runtime.find_service(fabric.first);
    const auto first_read = service->statistics(result.final_time);
    expect_equal(first_read.busy_time, sim::SimDurationNs{50});
    expect_equal(service->statistics(result.final_time), first_read);
}

TEST(TransportProgressTest, SwitchRecoveryAllowsOnlyNewWorkWithoutRetry) {
    Fabric fabric;
    TransportRuntime runtime{fabric.graph, fabric.config()};
    ProgressDispatcher dispatcher{runtime};
    int bootstraps{0};
    dispatcher.bootstrap = [&](sim::SimulationContext& context) {
        static_cast<void>(runtime.submit_transfer(fabric.request(100), context));
        if (bootstraps++ == 0) {
            static_cast<void>(runtime.schedule_switch_state_change(
                fabric.leaf, topology::OperationalState::Down, sim::SimTimeNs{50}, context));
            static_cast<void>(runtime.schedule_switch_state_change(
                fabric.leaf, topology::OperationalState::Up, sim::SimTimeNs{100}, context));
            static_cast<void>(context.schedule(
                {sim::SimTimeNs{100}, sim::EventPriority::Normal, sim::NoOpEvent{1}}));
        }
    };
    expect_equal(execute(dispatcher).status, sim::SimulationStatus::Completed);
    const auto outcomes = runtime.take_completed_transfers();
    ASSERT_EQ(outcomes.size(), 2U);
    expect_equal(outcomes[0].outcome, TransferOutcome::Failed);
    expect_equal(outcomes[1].outcome, TransferOutcome::Succeeded);
    expect_equal(outcomes[1].timestamp, sim::SimTimeNs{350});
}

} // namespace
} // namespace nexuslab::transport
