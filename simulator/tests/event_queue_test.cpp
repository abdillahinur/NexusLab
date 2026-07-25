// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/sim/event_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace nexuslab::sim {
namespace {

[[nodiscard]] Event make_event(std::uint64_t timestamp, EventPriority priority, std::uint64_t id,
                               std::uint64_t token, std::optional<EventId> cause = std::nullopt) {
    return Event{SimTimeNs{timestamp}, priority, EventId{id}, cause,
                 EventPayload{NoOpEvent{token}}};
}

TEST(EventIdGeneratorTest, AssignsMonotonicallyIncreasingIds) {
    static_assert(!std::is_copy_constructible_v<EventIdGenerator>);
    static_assert(!std::is_move_constructible_v<EventIdGenerator>);

    EventIdGenerator generator;

    EXPECT_EQ(generator.next(), EventId{0});
    EXPECT_EQ(generator.next(), EventId{1});
    EXPECT_EQ(generator.next(), EventId{2});
}

TEST(EventIdGeneratorTest, RejectsSequenceOverflow) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    EventIdGenerator generator{maximum};

    EXPECT_EQ(generator.next(), EventId{maximum});
    EXPECT_THROW(static_cast<void>(generator.next()), std::overflow_error);
}

TEST(EventQueueTest, EmptyQueueHasNoEvent) {
    EventQueue queue;

    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0U);
    EXPECT_EQ(queue.pop(), std::nullopt);
}

TEST(EventQueueTest, OrdersEventsByAscendingTimestamp) {
    EventQueue queue;
    const Event early = make_event(100, EventPriority::Normal, 2, 10);
    const Event middle = make_event(200, EventPriority::Normal, 1, 20);
    const Event late = make_event(300, EventPriority::Normal, 0, 30);

    queue.push(late);
    queue.push(early);
    queue.push(middle);

    EXPECT_EQ(queue.pop(), early);
    EXPECT_EQ(queue.pop(), middle);
    EXPECT_EQ(queue.pop(), late);
}

TEST(EventQueueTest, OrdersEqualTimestampsByAscendingPriority) {
    EventQueue queue;
    const Event critical = make_event(100, EventPriority::Critical, 3, 10);
    const Event control = make_event(100, EventPriority::Control, 2, 20);
    const Event normal = make_event(100, EventPriority::Normal, 1, 30);
    const Event background = make_event(100, EventPriority::Background, 0, 40);

    queue.push(background);
    queue.push(normal);
    queue.push(control);
    queue.push(critical);

    EXPECT_EQ(queue.pop(), critical);
    EXPECT_EQ(queue.pop(), control);
    EXPECT_EQ(queue.pop(), normal);
    EXPECT_EQ(queue.pop(), background);
}

TEST(EventQueueTest, OrdersEqualTimestampAndPriorityByAscendingId) {
    EventQueue queue;
    const Event first = make_event(100, EventPriority::Normal, 1, 10);
    const Event second = make_event(100, EventPriority::Normal, 2, 20);
    const Event third = make_event(100, EventPriority::Normal, 3, 30);

    queue.push(third);
    queue.push(first);
    queue.push(second);

    EXPECT_EQ(queue.pop(), first);
    EXPECT_EQ(queue.pop(), second);
    EXPECT_EQ(queue.pop(), third);
}

TEST(EventQueueTest, TimestampPrecedesPriorityAndIdInOrderingTuple) {
    EventQueue queue;
    const Event earlier = make_event(99, EventPriority::Background, 999, 10);
    const Event later = make_event(100, EventPriority::Critical, 0, 20);

    queue.push(later);
    queue.push(earlier);

    EXPECT_EQ(queue.pop(), earlier);
    EXPECT_EQ(queue.pop(), later);
}

TEST(EventQueueTest, OwnsAnIndependentEventValue) {
    EventQueue queue;
    Event submitted = make_event(100, EventPriority::Normal, 1, 10, EventId{0});
    const Event expected = submitted;

    queue.push(submitted);
    std::get<NoOpEvent>(submitted.payload).token = 99;
    submitted.cause = EventId{98};

    EXPECT_EQ(queue.pop(), expected);
    EXPECT_TRUE(queue.empty());
}

TEST(EventQueueTest, TracksSizeAcrossPushAndPop) {
    EventQueue queue;
    queue.push(make_event(100, EventPriority::Normal, 0, 10));
    queue.push(make_event(200, EventPriority::Normal, 1, 20));

    EXPECT_EQ(queue.size(), 2U);
    static_cast<void>(queue.pop());
    EXPECT_EQ(queue.size(), 1U);
    static_cast<void>(queue.pop());
    EXPECT_TRUE(queue.empty());
}

} // namespace
} // namespace nexuslab::sim
