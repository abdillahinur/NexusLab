// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/telemetry/session.hpp"
#include "nexuslab/transport/runtime.hpp"
#include "support/noop_dispatcher.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace nexuslab::telemetry {
namespace {

template <typename Value> [[nodiscard]] Value required(const std::optional<Value>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"required telemetry integration value is absent"};
    }
    return *value;
}

[[nodiscard]] MetricLabels link_labels(topology::DirectedLinkId link) {
    return {{MetricLabel::Link, link.link.value()},
            {MetricLabel::Direction, static_cast<std::uint64_t>(link.direction)}};
}

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(TelemetryIntegrationTest, SimulationEmitsLifecycleRecordsAndTerminalMetrics) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession telemetry{configuration};
    sim::Simulation simulation{42, sim::TraceMode::Disabled, telemetry.sink()};
    const sim::EventId dispatched =
        simulation.schedule({sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::NoOpEvent{1}});
    const sim::EventId cancelled =
        simulation.schedule({sim::SimTimeNs{20}, sim::EventPriority::Normal, sim::NoOpEvent{2}});
    ASSERT_TRUE(simulation.cancel(cancelled));
    auto dispatcher =
        nexuslab::test::NoOpDispatcher{[](const sim::NoOpEvent&, sim::SimulationContext& context) {
            static_cast<void>(context.random_u64());
        }};

    const sim::SimulationResult result = simulation.run(dispatcher);
    telemetry.finalize(result.final_time);

    ASSERT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::SimulationDispatchedEvents)).scalar, 1U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::SimulationCancelledEvents)).scalar, 1U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::SimulationFinalTimeNs)).scalar, 10U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::SimulationRngDraws)).scalar, 1U);
    EXPECT_EQ(required(telemetry.find_metric(
                           MetricId::SimulationTerminalTotal,
                           {{MetricLabel::Outcome,
                             static_cast<std::uint64_t>(sim::SimulationStatus::Completed)}}))
                  .scalar,
              1U);

    std::vector<SimulationTransition> transitions;
    for (const TelemetryRecord& record : telemetry.records()) {
        if (const auto* observation = std::get_if<SimulationObservation>(&record.observation)) {
            transitions.push_back(observation->transition);
            if (observation->transition == SimulationTransition::EventDispatched) {
                EXPECT_EQ(record.correlation.event, dispatched);
                EXPECT_FALSE(record.correlation.cause.has_value());
            }
        }
    }
    EXPECT_EQ(
        transitions,
        (std::vector{SimulationTransition::EventScheduled, SimulationTransition::EventScheduled,
                     SimulationTransition::EventCancelled, SimulationTransition::RunStarted,
                     SimulationTransition::EventDispatched, SimulationTransition::RunSucceeded}));
}

TEST(TelemetryIntegrationTest, TelemetryLimitFailsSimulationWithoutACompletedTraceRecord) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Summary;
    configuration.limits.metric_series = 1;
    TelemetrySession telemetry{configuration};
    sim::Simulation simulation{42, sim::TraceMode::Enabled, telemetry.sink()};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{1}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    auto dispatcher = nexuslab::test::NoOpDispatcher{
        [](const sim::NoOpEvent& /*event*/, sim::SimulationContext& /*context*/) {}};

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Failed);
    EXPECT_EQ(result.error, "telemetry metric series limit exceeded");
    ASSERT_FALSE(simulation.trace_records().empty());
    EXPECT_EQ(simulation.trace_records().back().action, sim::TraceAction::Failed);
    EXPECT_EQ(std::ranges::count_if(simulation.trace_records(),
                                    [](const sim::TraceRecord& record) {
                                        return record.action == sim::TraceAction::Completed;
                                    }),
              0);
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

    [[nodiscard]] std::vector<transport::DirectedLinkConfiguration> configurations() const {
        return {{first, transport::BitsPerSecond{8'000'000'000ULL}, sim::SimDurationNs{25},
                 transport::ByteCount{100}, transport::ByteCount{100}},
                {second, transport::BitsPerSecond{8'000'000'000ULL}, sim::SimDurationNs{25},
                 transport::ByteCount{100}, transport::ByteCount{100}}};
    }

    [[nodiscard]] transport::TransferRequest request() const {
        return {topology::NodeId{source},
                topology::NodeId{spine},
                transport::ByteCount{250},
                transport::ByteCount{100},
                {first, second}};
    }
};

class TransportDispatcher final {
  public:
    TransportDispatcher(transport::TransportRuntime& runtime, const Fabric& fabric,
                        bool fail_first_link = false)
        : runtime_{&runtime}, fabric_{&fabric}, fail_first_link_{fail_first_link} {}

    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) const {
        static_cast<void>(runtime_->submit_transfer(fabric_->request(), context));
        if (fail_first_link_) {
            static_cast<void>(runtime_->schedule_link_state_change(fabric_->first.link,
                                                                   topology::OperationalState::Down,
                                                                   sim::SimTimeNs{50}, context));
        }
    }
    void operator()(const transport::ChunkArrivalEvent& event,
                    sim::SimulationContext& context) const {
        runtime_->handle_arrival(event, context);
    }
    void operator()(const transport::TransmissionCompleteEvent& event,
                    sim::SimulationContext& context) const {
        runtime_->handle_completion(event, context);
    }
    void operator()(const transport::LinkStateChangeEvent& event,
                    sim::SimulationContext& context) const {
        runtime_->handle_link_state_change(event, context);
    }
    void operator()(const transport::PortStateChangeEvent& event,
                    sim::SimulationContext& context) const {
        runtime_->handle_port_state_change(event, context);
    }
    void operator()(const transport::SwitchStateChangeEvent& event,
                    sim::SimulationContext& context) const {
        runtime_->handle_switch_state_change(event, context);
    }
    void operator()(const workload::WorkloadEvent& /*event*/,
                    sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected workload event"};
    }
    void operator()(const collective::LocalCompletionEvent& /*event*/,
                    sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected collective event"};
    }

  private:
    transport::TransportRuntime* runtime_;
    const Fabric* fabric_;
    bool fail_first_link_;
};

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(TelemetryIntegrationTest, TransportEmitsQueueTransferAndLinkMetrics) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession telemetry{configuration};
    Fabric fabric;
    transport::TransportRuntime runtime{
        fabric.graph, fabric.configurations(), {}, telemetry.sink()};
    TransportDispatcher dispatcher{runtime, fabric};
    sim::Simulation simulation{42, sim::TraceMode::Disabled, telemetry.sink()};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));

    const sim::SimulationResult result = simulation.run(dispatcher);
    telemetry.finalize(result.final_time);

    ASSERT_EQ(result.status, sim::SimulationStatus::Completed);
    const MetricLabels first = link_labels(fabric.first);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkAcceptedBytes, first)).scalar, 200U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkAcceptedChunks, first)).scalar, 2U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkSerializedBytes, first)).scalar, 200U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkMarkedBytes, first)).scalar, 100U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkDroppedBufferFullBytes, first)).scalar,
              50U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkQueueWaitingBytes, first)).scalar, 0U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkMaximumWaitingBytes, first)).scalar,
              100U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkSerializerBusyNs, first)).scalar, 200U);
    EXPECT_EQ(required(telemetry.find_metric(
                           MetricId::TransferTerminalTotal,
                           {{MetricLabel::Outcome,
                             static_cast<std::uint64_t>(transport::TransferOutcome::Failed)}}))
                  .scalar,
              1U);

    std::size_t submitted = 0;
    std::size_t delivered = 0;
    std::size_t dropped = 0;
    std::size_t failed = 0;
    std::size_t correlated_queue_records = 0;
    for (const TelemetryRecord& record : telemetry.records()) {
        if (const auto* transfer = std::get_if<TransferObservation>(&record.observation)) {
            if (transfer->transition == TransferTransition::Submitted) {
                ++submitted;
            }
            if (transfer->transition == TransferTransition::ChunkDelivered) {
                ++delivered;
            }
            if (transfer->transition == TransferTransition::ChunkDropped) {
                ++dropped;
            }
            if (transfer->transition == TransferTransition::Failed) {
                ++failed;
            }
        }
        if (std::holds_alternative<QueueObservation>(record.observation) &&
            record.correlation.event.has_value() && record.correlation.transfer.has_value() &&
            record.correlation.chunk.has_value() && record.correlation.link.has_value()) {
            ++correlated_queue_records;
        }
    }
    EXPECT_EQ(submitted, 1U);
    EXPECT_EQ(delivered, 2U);
    EXPECT_EQ(dropped, 1U);
    EXPECT_EQ(failed, 1U);
    EXPECT_GT(correlated_queue_records, 0U);
}

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(TelemetryIntegrationTest, LinkFailureReportsDroppedTrafficAndPartialBusyTime) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession telemetry{configuration};
    Fabric fabric;
    auto links = fabric.configurations();
    for (auto& link : links) {
        link.waiting_buffer_capacity = transport::ByteCount{1'000};
    }
    transport::TransportRuntime runtime{fabric.graph, links, {}, telemetry.sink()};
    TransportDispatcher dispatcher{runtime, fabric, true};
    sim::Simulation simulation{42, sim::TraceMode::Disabled, telemetry.sink()};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));

    const sim::SimulationResult result = simulation.run(dispatcher);
    telemetry.finalize(result.final_time);

    ASSERT_EQ(result.status, sim::SimulationStatus::Completed);
    const MetricLabels first = link_labels(fabric.first);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkDroppedDownBytes, first)).scalar, 250U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkDroppedDownChunks, first)).scalar, 3U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkSerializerBusyNs, first)).scalar, 50U);
    EXPECT_EQ(required(telemetry.find_metric(MetricId::LinkQueueWaitingBytes, first)).scalar, 0U);
    const auto dropped_records = std::ranges::count_if(
        telemetry.records(), [&first_link = fabric.first](const TelemetryRecord& record) {
            const auto* transfer = std::get_if<TransferObservation>(&record.observation);
            return transfer != nullptr &&
                   transfer->transition == TransferTransition::ChunkDropped &&
                   transfer->reason == TransferReason::ResourceDown &&
                   record.correlation.link == first_link;
        });
    EXPECT_EQ(dropped_records, 3);
}

} // namespace
} // namespace nexuslab::telemetry
