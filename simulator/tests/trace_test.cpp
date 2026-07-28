// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/sim/trace.hpp"
#include "support/noop_dispatcher.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nexuslab::sim {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

[[nodiscard]] EventSpec no_op_at(std::uint64_t timestamp, std::uint64_t token) {
    return EventSpec{SimTimeNs{timestamp}, EventPriority::Normal, EventPayload{NoOpEvent{token}}};
}

[[nodiscard]] TraceRecord event_record(TraceAction action, SimTimeNs recorded_at, EventId event_id,
                                       SimTimeNs event_timestamp) {
    return TraceRecord{action,
                       recorded_at,
                       event_id,
                       std::nullopt,
                       event_timestamp,
                       EventPriority::Normal,
                       EventPayloadKind::NoOp,
                       std::nullopt,
                       std::nullopt};
}

TEST(TraceLogTest, UsesStableExplicitLittleEndianEncoding) {
    TraceLog trace;
    trace.append(TraceRecord{TraceAction::Scheduled, SimTimeNs{5}, EventId{7}, std::nullopt,
                             SimTimeNs{11}, EventPriority::Control, EventPayloadKind::NoOp,
                             std::nullopt, std::nullopt});

    EXPECT_EQ(trace.hash(), 10'709'538'336'127'559'684ULL);
}

TEST(TraceLogTest, DisabledLogStoresNothingAndHasNoHash) {
    TraceLog trace{TraceMode::Disabled};
    trace.append(event_record(TraceAction::Scheduled, SimTimeNs{0}, EventId{0}, SimTimeNs{100}));

    EXPECT_THAT(trace.records(), IsEmpty());
    EXPECT_EQ(trace.hash(), std::nullopt);
}

TEST(SimulationTraceTest, RecordsScheduleDispatchAndCompletion) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    auto dispatcher = nexuslab::test::NoOpDispatcher{[](const NoOpEvent&, SimulationContext&) {}};

    const SimulationResult result = simulation.run(dispatcher);

    const TraceRecord completed{TraceAction::Completed, SimTimeNs{100}, std::nullopt,
                                std::nullopt,           std::nullopt,   std::nullopt,
                                std::nullopt,           std::nullopt,   std::nullopt};
    EXPECT_THAT(
        simulation.trace_records(),
        ElementsAre(
            event_record(TraceAction::Scheduled, SimTimeNs{0}, EventId{0}, SimTimeNs{100}),
            event_record(TraceAction::Dispatched, SimTimeNs{100}, EventId{0}, SimTimeNs{100}),
            completed));
    EXPECT_TRUE(result.trace_hash.has_value());
}

TEST(SimulationTraceTest, RecordsCancellationAndStopWithEventMetadata) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    const EventId cancelled = simulation.schedule(no_op_at(200, 2));
    auto dispatcher =
        nexuslab::test::NoOpDispatcher{[cancelled](const NoOpEvent&, SimulationContext& context) {
            static_cast<void>(context.cancel(cancelled));
            context.stop(StopReason::Requested);
        }};

    const SimulationResult result = simulation.run(dispatcher);

    ASSERT_EQ(result.status, SimulationStatus::Stopped);
    ASSERT_EQ(simulation.trace_records().size(), 5U);
    const TraceRecord& cancellation = simulation.trace_records()[3];
    EXPECT_EQ(cancellation,
              event_record(TraceAction::Cancelled, SimTimeNs{100}, cancelled, SimTimeNs{200}));

    TraceRecord expected_stop =
        event_record(TraceAction::StopRequested, SimTimeNs{100}, EventId{0}, SimTimeNs{100});
    expected_stop.stop_reason = StopReason::Requested;
    EXPECT_EQ(simulation.trace_records()[4], expected_stop);
}

TEST(SimulationTraceTest, RecordsFailureDetailsAndCurrentEvent) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    auto dispatcher = nexuslab::test::NoOpDispatcher{
        [](const NoOpEvent&, SimulationContext&) { throw std::runtime_error{"handler failed"}; }};

    const SimulationResult result = simulation.run(dispatcher);

    ASSERT_EQ(result.status, SimulationStatus::Failed);
    ASSERT_EQ(simulation.trace_records().size(), 3U);
    TraceRecord expected_failure =
        event_record(TraceAction::Failed, SimTimeNs{100}, EventId{0}, SimTimeNs{100});
    expected_failure.error = std::string{"handler failed"};
    EXPECT_EQ(simulation.trace_records()[2], expected_failure);
}

TEST(SimulationTraceTest, CanDisableTracingForBenchmarks) {
    Simulation simulation{42, TraceMode::Disabled};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    auto dispatcher = nexuslab::test::NoOpDispatcher{[](const NoOpEvent&, SimulationContext&) {}};

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_THAT(simulation.trace_records(), IsEmpty());
    EXPECT_EQ(result.trace_hash, std::nullopt);
}

TEST(SimulationTraceTest, TenIdenticalSeededRunsProduceTheSameHash) {
    std::optional<std::uint64_t> expected_hash;

    for (std::uint64_t run = 0; run < 10; ++run) {
        Simulation simulation{42};
        static_cast<void>(simulation.schedule(no_op_at(1, 0)));
        auto dispatcher =
            nexuslab::test::NoOpDispatcher{[](const NoOpEvent& event, SimulationContext& context) {
                if (event.token < 4) {
                    const std::uint64_t delay = context.random_below(10) + 1;
                    static_cast<void>(
                        context.schedule(no_op_at(context.now().count() + delay, event.token + 1)));
                }
            }};

        const SimulationResult result = simulation.run(dispatcher);

        ASSERT_TRUE(result.trace_hash.has_value());
        if (run == 0) {
            expected_hash = result.trace_hash;
        }
        EXPECT_EQ(result.trace_hash, expected_hash);
    }
}

} // namespace
} // namespace nexuslab::sim
