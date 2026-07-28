// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/link_queue.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace nexuslab::transport {
namespace {

[[nodiscard]] DirectedLinkConfiguration
configuration(topology::LinkDirection direction = topology::LinkDirection::AToB,
              std::uint64_t capacity = 100, std::optional<std::uint64_t> threshold = std::nullopt) {
    return DirectedLinkConfiguration{
        topology::DirectedLinkId{topology::LinkId{7}, direction},
        BitsPerSecond{100'000'000'000ULL},
        sim::SimDurationNs{250},
        ByteCount{capacity},
        threshold.has_value() ? std::optional<ByteCount>{ByteCount{*threshold}} : std::nullopt,
    };
}

struct ChunkValues final {
    std::uint64_t id;
    std::uint64_t bytes;
    std::uint64_t transfer{0};
    std::uint32_t hop{0};
    bool marked{false};
};

[[nodiscard]] TransferChunk chunk(ChunkValues values) {
    return TransferChunk{TransferId{values.transfer}, ChunkId{values.id}, ByteCount{values.bytes},
                         values.hop, values.marked};
}

TEST(DirectedLinkQueueTest, PreservesImmutablePerDirectionConfiguration) {
    const DirectedLinkConfiguration forward = configuration(topology::LinkDirection::AToB);
    const DirectedLinkConfiguration reverse = configuration(topology::LinkDirection::BToA, 200, 75);
    const DirectedLinkQueue forward_queue{forward};
    const DirectedLinkQueue reverse_queue{reverse};

    EXPECT_EQ(forward_queue.configuration(), forward);
    EXPECT_EQ(reverse_queue.configuration(), reverse);
    EXPECT_NE(forward_queue.configuration(), reverse_queue.configuration());
}

TEST(DirectedLinkQueueTest, RejectsInvalidConfiguration) {
    DirectedLinkConfiguration invalid_bandwidth = configuration();
    invalid_bandwidth.bandwidth = BitsPerSecond{0};
    DirectedLinkConfiguration zero_threshold = configuration();
    zero_threshold.marking_threshold = ByteCount{0};
    DirectedLinkConfiguration excessive_threshold = configuration();
    excessive_threshold.marking_threshold = ByteCount{101};

    EXPECT_THROW(static_cast<void>(DirectedLinkQueue{invalid_bandwidth}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(DirectedLinkQueue{zero_threshold}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(DirectedLinkQueue{excessive_threshold}), std::invalid_argument);
}

TEST(DirectedLinkQueueTest, StartsIdleServiceWithoutChargingWaitingCapacity) {
    DirectedLinkQueue queue{configuration(topology::LinkDirection::AToB, 0)};
    const TransferChunk oversized = chunk({1, 1'000});

    EXPECT_EQ(queue.admit(oversized),
              (AdmissionResult{AdmissionDisposition::ServiceStarted, false}));
    ASSERT_NE(queue.active(), nullptr);
    EXPECT_EQ(*queue.active(), oversized);
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{0}, 0, true}));

    EXPECT_EQ(queue.admit(chunk({2, 1})),
              (AdmissionResult{AdmissionDisposition::DroppedBufferFull, false}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{0}, 0, true}));
}

TEST(DirectedLinkQueueTest, AcceptsExactCapacityThenTailDropsAtomically) {
    DirectedLinkQueue queue{configuration()};
    static_cast<void>(queue.admit(chunk({0, 500})));

    EXPECT_EQ(queue.admit(chunk({1, 60})), (AdmissionResult{AdmissionDisposition::Queued, false}));
    EXPECT_EQ(queue.admit(chunk({2, 40})), (AdmissionResult{AdmissionDisposition::Queued, false}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{100}, 2, ByteCount{100}, 2, true}));

    EXPECT_EQ(queue.admit(chunk({3, 1})),
              (AdmissionResult{AdmissionDisposition::DroppedBufferFull, false}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{100}, 2, ByteCount{100}, 2, true}));
}

TEST(DirectedLinkQueueTest, ChecksCapacityWithoutUnsignedAdditionOverflow) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    DirectedLinkQueue queue{configuration(topology::LinkDirection::AToB, maximum)};
    static_cast<void>(queue.admit(chunk({0, 1})));

    EXPECT_EQ(queue.admit(chunk({1, maximum - 1})),
              (AdmissionResult{AdmissionDisposition::Queued, false}));
    EXPECT_EQ(queue.admit(chunk({2, 2})),
              (AdmissionResult{AdmissionDisposition::DroppedBufferFull, false}));
    EXPECT_EQ(queue.snapshot(),
              (QueueSnapshot{ByteCount{maximum - 1}, 1, ByteCount{maximum - 1}, 1, true}));
}

TEST(DirectedLinkQueueTest, MarksAcceptedWaitingChunksAtConfiguredThreshold) {
    DirectedLinkQueue queue{configuration(topology::LinkDirection::AToB, 100, 75)};
    static_cast<void>(queue.admit(chunk({0, 500})));

    EXPECT_EQ(queue.admit(chunk({1, 60})), (AdmissionResult{AdmissionDisposition::Queued, false}));
    EXPECT_EQ(queue.admit(chunk({2, 15})), (AdmissionResult{AdmissionDisposition::Queued, true}));
    EXPECT_EQ(queue.admit(chunk({3, 25, 0, 0, true})),
              (AdmissionResult{AdmissionDisposition::Queued, true}));

    static_cast<void>(queue.complete_service());
    ASSERT_NE(queue.active(), nullptr);
    EXPECT_FALSE(queue.active()->marked);
    static_cast<void>(queue.complete_service());
    ASSERT_NE(queue.active(), nullptr);
    EXPECT_TRUE(queue.active()->marked);
}

TEST(DirectedLinkQueueTest, PromotesWaitingChunksInFifoOrderAcrossTransfers) {
    DirectedLinkQueue queue{configuration()};
    const TransferChunk first = chunk({0, 10, 4});
    const TransferChunk second = chunk({1, 20, 9});
    const TransferChunk third = chunk({2, 30, 4});

    static_cast<void>(queue.admit(first));
    static_cast<void>(queue.admit(second));
    static_cast<void>(queue.admit(third));

    EXPECT_EQ(queue.complete_service(), (ServiceTransition{first, second}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{30}, 1, ByteCount{50}, 2, true}));
    EXPECT_EQ(queue.complete_service(), (ServiceTransition{second, third}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{50}, 2, true}));
    EXPECT_EQ(queue.complete_service(), (ServiceTransition{third, std::nullopt}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{50}, 2, false}));
    EXPECT_FALSE(queue.complete_service().has_value());
}

TEST(DirectedLinkQueueTest, RejectsZeroByteChunkWithoutMutation) {
    DirectedLinkQueue queue{configuration()};

    EXPECT_THROW(static_cast<void>(queue.admit(chunk({0, 0}))), std::invalid_argument);
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{0}, 0, false}));
    EXPECT_EQ(queue.active(), nullptr);
}

TEST(DirectedLinkQueueTest, OppositeDirectionsMaintainIndependentState) {
    DirectedLinkQueue forward{configuration(topology::LinkDirection::AToB)};
    DirectedLinkQueue reverse{configuration(topology::LinkDirection::BToA)};

    static_cast<void>(forward.admit(chunk({0, 10})));
    static_cast<void>(forward.admit(chunk({1, 40})));
    static_cast<void>(reverse.admit(chunk({2, 20})));

    EXPECT_EQ(forward.snapshot(), (QueueSnapshot{ByteCount{40}, 1, ByteCount{40}, 1, true}));
    EXPECT_EQ(reverse.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{0}, 0, true}));
}

TEST(DirectedLinkQueueTest, DrainsActiveAndWaitingChunksInFifoOrder) {
    DirectedLinkQueue queue{configuration()};
    const TransferChunk active = chunk({0, 10});
    const TransferChunk first_waiting = chunk({1, 20});
    const TransferChunk second_waiting = chunk({2, 30});
    static_cast<void>(queue.admit(active));
    static_cast<void>(queue.admit(first_waiting));
    static_cast<void>(queue.admit(second_waiting));

    EXPECT_EQ(queue.drain(),
              (QueueDrain{active, std::vector<TransferChunk>{first_waiting, second_waiting}}));
    EXPECT_EQ(queue.snapshot(), (QueueSnapshot{ByteCount{0}, 0, ByteCount{50}, 2, false}));
    EXPECT_EQ(queue.drain(), (QueueDrain{std::nullopt, {}}));
}

} // namespace
} // namespace nexuslab::transport
