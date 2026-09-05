// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/sim/trace.hpp"
#include "nexuslab/transport/link_service.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nexuslab::transport {
namespace {

[[nodiscard]] DirectedLinkConfiguration service_configuration() {
    return DirectedLinkConfiguration{
        topology::DirectedLinkId{topology::LinkId{4}, topology::LinkDirection::AToB},
        BitsPerSecond{8'000'000'000ULL},
        sim::SimDurationNs{25},
        ByteCount{1'000},
        std::nullopt,
    };
}

[[nodiscard]] TransferChunk service_chunk(std::uint64_t id, std::uint64_t bytes) {
    return TransferChunk{TransferId{3}, ChunkId{id}, ByteCount{bytes}, 0, false};
}

class ServiceDispatcher final {
  public:
    ServiceDispatcher(DirectedLinkService& service, std::vector<TransferChunk> submissions)
        : service_{&service}, submissions_{std::move(submissions)} {}

    void operator()(const sim::NoOpEvent& event, sim::SimulationContext& context) {
        static_cast<void>(event);
        for (const TransferChunk& chunk : submissions_) {
            admissions_.push_back(service_->admit(chunk, context));
        }
    }

    void operator()(const ChunkArrivalEvent& event, sim::SimulationContext& context) {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"chunk arrival is not integrated in this service slice"};
    }

    void operator()(const TransmissionCompleteEvent& event, sim::SimulationContext& context) {
        const ServiceTransition transition = service_->handle_completion(event, context);
        completed_.push_back(transition.completed);
        completion_times_.push_back(context.now());
    }

    void operator()(const LinkStateChangeEvent& event, sim::SimulationContext& context) {
        static_cast<void>(event);
        static_cast<void>(context);
        throw std::logic_error{"link-state changes are not integrated in this service slice"};
    }

    [[nodiscard]] const std::vector<AdmissionResult>& admissions() const noexcept {
        return admissions_;
    }

    [[nodiscard]] const std::vector<TransferChunk>& completed() const noexcept {
        return completed_;
    }

    [[nodiscard]] const std::vector<sim::SimTimeNs>& completion_times() const noexcept {
        return completion_times_;
    }

    void operator()(const nexuslab::transport::PortStateChangeEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected port-state event in this dispatcher"};
    }

    void operator()(const nexuslab::transport::SwitchStateChangeEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected switch-state event in this dispatcher"};
    }
    void operator()(const nexuslab::workload::WorkloadEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected workload event in this dispatcher"};
    }
    void operator()(const nexuslab::collective::LocalCompletionEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected collective event in this dispatcher"};
    }

  private:
    DirectedLinkService* service_;
    std::vector<TransferChunk> submissions_;
    std::vector<AdmissionResult> admissions_;
    std::vector<TransferChunk> completed_;
    std::vector<sim::SimTimeNs> completion_times_;
};

TEST(TransportEventTest, UsesExplicitStablePayloadKindsWithinEventSizeBudget) {
    const ChunkArrivalEvent arrival{ChunkId{2}, 3};
    const TransmissionCompleteEvent completion{
        topology::LinkId{4},
        ChunkId{2},
        3,
        topology::LinkDirection::BToA,
    };
    const LinkStateChangeEvent state_change{
        topology::LinkId{4},
        topology::OperationalState::Down,
    };

    EXPECT_EQ(sim::payload_kind(sim::EventPayload{arrival}), sim::EventPayloadKind::ChunkArrival);
    EXPECT_EQ(sim::payload_kind(sim::EventPayload{completion}),
              sim::EventPayloadKind::TransmissionComplete);
    EXPECT_EQ(sim::payload_kind(sim::EventPayload{state_change}),
              sim::EventPayloadKind::LinkStateChange);
    EXPECT_EQ(static_cast<std::uint8_t>(sim::EventPayloadKind::ChunkArrival), 2U);
    EXPECT_EQ(static_cast<std::uint8_t>(sim::EventPayloadKind::TransmissionComplete), 3U);
    EXPECT_EQ(static_cast<std::uint8_t>(sim::EventPayloadKind::LinkStateChange), 4U);
    EXPECT_LE(sizeof(sim::EventPayload), 32U);
    EXPECT_LE(sizeof(sim::Event), 80U);
}

// GTest assertion macros inflate clang-tidy's cognitive-complexity count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(DirectedLinkServiceTest, SchedulesExactControlPriorityCompletionsAndPromotesFifo) {
    DirectedLinkService service{service_configuration()};
    const TransferChunk first = service_chunk(0, 100);
    const TransferChunk second = service_chunk(1, 200);
    ServiceDispatcher dispatcher{service, {first, second}};
    sim::Simulation simulation{42};
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{10}, sim::EventPriority::Normal, sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, sim::SimTimeNs{310});
    EXPECT_EQ(result.dispatched_events, 3U);
    EXPECT_EQ(dispatcher.admissions(),
              (std::vector<AdmissionResult>{
                  AdmissionResult{AdmissionDisposition::ServiceStarted, false},
                  AdmissionResult{AdmissionDisposition::Queued, false},
              }));
    EXPECT_EQ(dispatcher.completed(), (std::vector<TransferChunk>{first, second}));
    EXPECT_EQ(dispatcher.completion_times(),
              (std::vector<sim::SimTimeNs>{sim::SimTimeNs{110}, sim::SimTimeNs{310}}));
    EXPECT_FALSE(service.scheduled_completion().has_value());
    EXPECT_EQ(service.queue().snapshot(),
              (QueueSnapshot{ByteCount{0}, 0, ByteCount{200}, 1, false}));

    std::size_t completion_schedules{0};
    for (const sim::TraceRecord& record : simulation.trace_records()) {
        if (record.action == sim::TraceAction::Scheduled &&
            record.payload_kind == sim::EventPayloadKind::TransmissionComplete) {
            ++completion_schedules;
            EXPECT_EQ(record.priority, sim::EventPriority::Control);
        }
    }
    EXPECT_EQ(completion_schedules, 2U);
}

TEST(DirectedLinkServiceTest, RejectsUnexpectedCompletionWithoutActiveService) {
    DirectedLinkService service{service_configuration()};
    ServiceDispatcher dispatcher{service, {}};
    sim::Simulation simulation{42};
    const TransmissionCompleteEvent unexpected{
        service_configuration().link.link,
        ChunkId{0},
        0,
        service_configuration().link.direction,
    };
    static_cast<void>(simulation.schedule(sim::EventSpec{
        sim::SimTimeNs{1}, sim::EventPriority::Control, sim::EventPayload{unexpected}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Failed);
    EXPECT_EQ(result.error, std::string{"unexpected transmission-completion event"});
    EXPECT_FALSE(service.queue().snapshot().busy);
}

TEST(DirectedLinkServiceTest, RejectsCompletionTimeOverflowBeforeQueueMutation) {
    DirectedLinkService service{service_configuration()};
    ServiceDispatcher dispatcher{service, {service_chunk(0, 1)}};
    sim::Simulation simulation{42};
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    static_cast<void>(
        simulation.schedule(sim::EventSpec{sim::SimTimeNs{maximum}, sim::EventPriority::Normal,
                                           sim::EventPayload{sim::NoOpEvent{0}}}));

    const sim::SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, sim::SimulationStatus::Failed);
    EXPECT_EQ(result.error, std::string{"serialization completion exceeds simulated-time range"});
    EXPECT_EQ(service.queue().snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{0}, 0, false}));
    EXPECT_FALSE(service.scheduled_completion().has_value());
}

TEST(DirectedLinkServiceTest, PromotionOverflowLeavesQueueAndCountersUnchanged) {
    DirectedLinkService service{service_configuration()};
    ServiceDispatcher dispatcher{service, {service_chunk(0, 100), service_chunk(1, 100)}};
    sim::Simulation simulation{42};
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    static_cast<void>(simulation.schedule(
        {sim::SimTimeNs{maximum - 150}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    const auto result = simulation.run(dispatcher);
    EXPECT_EQ(result.status, sim::SimulationStatus::Failed);
    EXPECT_EQ(result.error, std::string{"serialization completion exceeds simulated-time range"});
    ASSERT_NE(service.queue().active(), nullptr);
    EXPECT_EQ(service.queue().active()->id, ChunkId{0});
    EXPECT_EQ(service.queue().snapshot().waiting_bytes, ByteCount{100});
    EXPECT_EQ(service.statistics(result.final_time).completed, TrafficCount{});
    EXPECT_EQ(service.statistics(result.final_time).started, (TrafficCount{100, 1}));
}

TEST(TransportEventTest, PortAndSwitchKindsAreStableAndWithinBudget) {
    const auto port = sim::EventPayload{
        PortStateChangeEvent{topology::PortId{1}, topology::OperationalState::Down}};
    const auto network_switch = sim::EventPayload{
        SwitchStateChangeEvent{topology::SwitchId{1}, topology::OperationalState::Up}};
    EXPECT_EQ(static_cast<std::uint8_t>(sim::payload_kind(port)), 5U);
    EXPECT_EQ(static_cast<std::uint8_t>(sim::payload_kind(network_switch)), 6U);
    EXPECT_LE(sizeof(sim::Event), 80U);
}

} // namespace
} // namespace nexuslab::transport
