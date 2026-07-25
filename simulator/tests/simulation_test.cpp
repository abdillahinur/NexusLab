// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace nexuslab::sim {
namespace {

[[nodiscard]] EventSpec no_op_at(std::uint64_t timestamp, std::uint64_t token,
                                 EventPriority priority = EventPriority::Normal) {
    return EventSpec{SimTimeNs{timestamp}, priority, EventPayload{NoOpEvent{token}}};
}

template <typename Operation> [[nodiscard]] bool throws_logic_error(Operation&& operation) {
    try {
        std::forward<Operation>(operation)();
    } catch (const std::logic_error&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

TEST(SimulationTest, EmptyQueueCompletesAtTimeZero) {
    Simulation simulation{42};
    auto dispatcher = [](const NoOpEvent&, SimulationContext&) {};

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result, (SimulationResult{SimulationStatus::Completed, std::nullopt, SimTimeNs{0}, 0,
                                        0, 0, 0, result.trace_hash, std::nullopt}));
    EXPECT_TRUE(result.trace_hash.has_value());
}

TEST(SimulationTest, DispatchesInQueueOrderAndAdvancesClock) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(300, 3)));
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    static_cast<void>(simulation.schedule(no_op_at(200, 2)));
    std::vector<std::uint64_t> tokens;
    std::vector<SimTimeNs> times;
    auto dispatcher = [&tokens, &times](const NoOpEvent& event, SimulationContext& context) {
        tokens.push_back(event.token);
        times.push_back(context.now());
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(tokens, (std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT_EQ(times, (std::vector<SimTimeNs>{SimTimeNs{100}, SimTimeNs{200}, SimTimeNs{300}}));
    EXPECT_EQ(result.status, SimulationStatus::Completed);
    EXPECT_EQ(result.final_time, SimTimeNs{300});
    EXPECT_EQ(result.dispatched_events, 3U);
}

TEST(SimulationTest, EventCanScheduleAnotherEventAtCurrentTime) {
    Simulation simulation{42};
    const EventId first_id = simulation.schedule(no_op_at(100, 1));
    const EventId existing_id = simulation.schedule(no_op_at(100, 2));
    std::vector<std::uint64_t> tokens;
    std::vector<EventId> ids;
    std::optional<EventId> scheduled_id;
    std::optional<EventId> scheduled_cause;
    auto dispatcher = [&](const NoOpEvent& event, SimulationContext& context) {
        tokens.push_back(event.token);
        ids.push_back(context.current_event_id());
        if (event.token == 1) {
            scheduled_id = context.schedule(no_op_at(context.now().count(), 3));
        } else if (event.token == 3) {
            scheduled_cause = context.cause();
        }
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, SimulationStatus::Completed);
    EXPECT_EQ(tokens, (std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT_EQ(scheduled_id, EventId{2});
    EXPECT_EQ(ids, (std::vector<EventId>{first_id, existing_id, EventId{2}}));
    EXPECT_EQ(scheduled_cause, first_id);
}

TEST(SimulationTest, RejectsSchedulingInThePastDuringDispatch) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    auto dispatcher = [](const NoOpEvent&, SimulationContext& context) {
        static_cast<void>(context.schedule(no_op_at(99, 2)));
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, SimulationStatus::Failed);
    EXPECT_EQ(result.final_time, SimTimeNs{100});
    EXPECT_EQ(result.dispatched_events, 1U);
    EXPECT_EQ(result.error, std::string{"cannot schedule an event in the simulated past"});
}

TEST(SimulationTest, CancelsPendingEventsBeforeRun) {
    Simulation simulation{42};
    const EventId dispatched = simulation.schedule(no_op_at(100, 1));
    const EventId cancelled = simulation.schedule(no_op_at(200, 2));
    std::vector<EventId> ids;
    auto dispatcher = [&ids](const NoOpEvent&, SimulationContext& context) {
        ids.push_back(context.current_event_id());
    };

    const bool first_cancellation = simulation.cancel(cancelled);
    const bool repeated_cancellation = simulation.cancel(cancelled);
    const bool unknown_cancellation = simulation.cancel(EventId{999});
    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ((std::tuple{first_cancellation, repeated_cancellation, unknown_cancellation}),
              (std::tuple{true, false, false}));
    EXPECT_EQ(ids, (std::vector<EventId>{dispatched}));
    EXPECT_EQ(result, (SimulationResult{SimulationStatus::Completed, std::nullopt, SimTimeNs{100},
                                        1, 1, 0, 0, result.trace_hash, std::nullopt}));
    EXPECT_TRUE(result.trace_hash.has_value());
}

TEST(SimulationTest, EventCanCancelAnotherPendingEvent) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    const EventId cancelled = simulation.schedule(no_op_at(200, 2));
    std::vector<std::uint64_t> tokens;
    auto dispatcher = [&](const NoOpEvent& event, SimulationContext& context) {
        tokens.push_back(event.token);
        if (event.token == 1) {
            EXPECT_TRUE(context.cancel(cancelled));
        }
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(tokens, (std::vector<std::uint64_t>{1}));
    EXPECT_EQ(result.status, SimulationStatus::Completed);
    EXPECT_EQ(result.cancelled_events, 1U);
}

TEST(SimulationTest, CancelledHeapEntriesAreNotReportedAsPendingAfterStop) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    const EventId cancelled = simulation.schedule(no_op_at(200, 2));
    auto dispatcher = [cancelled](const NoOpEvent&, SimulationContext& context) {
        EXPECT_TRUE(context.cancel(cancelled));
        context.stop(StopReason::Requested);
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, SimulationStatus::Stopped);
    EXPECT_EQ(result.cancelled_events, 1U);
    EXPECT_EQ(result.pending_events, 0U);
}

TEST(SimulationTest, StopFinishesCurrentHandlerAndLeavesPendingEvents) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    static_cast<void>(simulation.schedule(no_op_at(200, 2)));
    std::vector<std::uint64_t> tokens;
    auto dispatcher = [&tokens](const NoOpEvent& event, SimulationContext& context) {
        tokens.push_back(event.token);
        context.stop(StopReason::Requested);
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(tokens, (std::vector<std::uint64_t>{1}));
    EXPECT_EQ(result.status, SimulationStatus::Stopped);
    EXPECT_EQ(result.stop_reason, StopReason::Requested);
    EXPECT_EQ(result.final_time, SimTimeNs{100});
    EXPECT_EQ(result.dispatched_events, 1U);
    EXPECT_EQ(result.pending_events, 1U);
}

TEST(SimulationTest, HandlerFailureProducesFailedResult) {
    Simulation simulation{42};
    static_cast<void>(simulation.schedule(no_op_at(100, 1)));
    auto dispatcher = [](const NoOpEvent&, SimulationContext&) {
        throw std::runtime_error{"handler failed"};
    };

    const SimulationResult result = simulation.run(dispatcher);

    EXPECT_EQ(result.status, SimulationStatus::Failed);
    EXPECT_EQ(result.final_time, SimTimeNs{100});
    EXPECT_EQ(result.dispatched_events, 1U);
    EXPECT_EQ(result.error, std::string{"handler failed"});
}

TEST(SimulationTest, SeededRandomDrawsAreRepeatable) {
    std::vector<std::uint64_t> first_run;
    std::vector<std::uint64_t> second_run;

    const auto execute = [](std::vector<std::uint64_t>& draws) {
        Simulation simulation{42};
        static_cast<void>(simulation.schedule(no_op_at(100, 1)));
        static_cast<void>(simulation.schedule(no_op_at(200, 2)));
        auto dispatcher = [&draws](const NoOpEvent&, SimulationContext& context) {
            draws.push_back(context.random_u64());
            draws.push_back(context.random_below(10));
        };
        return simulation.run(dispatcher);
    };

    const SimulationResult first_result = execute(first_run);
    const SimulationResult second_result = execute(second_run);

    EXPECT_EQ(first_run, second_run);
    EXPECT_EQ(first_result.rng_draw_count, 4U);
    EXPECT_EQ(second_result.rng_draw_count, 4U);
}

TEST(SimulationTest, FinishedSimulationRejectsReuse) {
    Simulation simulation{42};
    auto dispatcher = [](const NoOpEvent&, SimulationContext&) {};
    const SimulationResult result = simulation.run(dispatcher);
    const bool rerun_rejected = throws_logic_error(
        [&simulation, &dispatcher] { static_cast<void>(simulation.run(dispatcher)); });
    const bool schedule_rejected = throws_logic_error(
        [&simulation] { static_cast<void>(simulation.schedule(no_op_at(100, 1))); });
    const bool stop_rejected =
        throws_logic_error([&simulation] { simulation.stop(StopReason::Requested); });

    EXPECT_EQ(result.status, SimulationStatus::Completed);
    EXPECT_EQ((std::tuple{rerun_rejected, schedule_rejected, stop_rejected}),
              (std::tuple{true, true, true}));
}

} // namespace
} // namespace nexuslab::sim
