// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/timing.hpp"
#include "nexuslab/transport/types.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace nexuslab::transport {
namespace {

static_assert(!std::is_same_v<TransferId, ChunkId>);
static_assert(!std::is_same_v<ByteCount, BitsPerSecond>);
static_assert(!std::is_copy_constructible_v<SequentialTransportIdGenerator<TransferId>>);

TEST(TransportTypesTest, KeepsIdentityAndQuantityKindsDistinctAndOrdered) {
    constexpr TransferId first_transfer{4};
    constexpr TransferId second_transfer{5};
    constexpr ChunkId chunk{4};
    constexpr ByteCount bytes{4};
    constexpr BitsPerSecond bandwidth{4};

    EXPECT_LT(first_transfer, second_transfer);
    EXPECT_EQ(first_transfer.value(), 4U);
    EXPECT_EQ(chunk.value(), 4U);
    EXPECT_EQ(bytes.value(), 4U);
    EXPECT_EQ(bandwidth.value(), 4U);
}

TEST(TransportIdGeneratorTest, ReservesRangesAndRejectsExhaustion) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    SequentialTransportIdGenerator<ChunkId> generator{maximum - 1};

    EXPECT_TRUE(generator.can_generate(2));
    EXPECT_FALSE(generator.can_generate(3));
    EXPECT_EQ(generator.next(), ChunkId{maximum - 1});
    EXPECT_EQ(generator.next(), ChunkId{maximum});
    EXPECT_FALSE(generator.can_generate(1));
    EXPECT_THROW(static_cast<void>(generator.next()), std::overflow_error);
}

TEST(TransportIdGeneratorTest, AdvancesPastCallerSuppliedIdentifiers) {
    SequentialTransportIdGenerator<TransferId> generator;

    generator.advance_past(TransferId{7});
    EXPECT_EQ(generator.next(), TransferId{8});
    generator.advance_past(TransferId{3});
    EXPECT_EQ(generator.next(), TransferId{9});
}

TEST(SerializationDelayTest, MatchesExactAnalyticalResults) {
    EXPECT_EQ(serialization_delay(ByteCount{1}, BitsPerSecond{8'000'000'000ULL}),
              sim::SimDurationNs{1});
    EXPECT_EQ(serialization_delay(ByteCount{1'500}, BitsPerSecond{100'000'000'000ULL}),
              sim::SimDurationNs{120});
    EXPECT_EQ(serialization_delay(ByteCount{1'000'000}, BitsPerSecond{400'000'000'000ULL}),
              sim::SimDurationNs{20'000});
}

TEST(SerializationDelayTest, RoundsPositiveFractionalNanosecondsUp) {
    EXPECT_EQ(serialization_delay(ByteCount{1}, BitsPerSecond{100'000'000'000ULL}),
              sim::SimDurationNs{1});
    EXPECT_EQ(serialization_delay(ByteCount{1'048'576}, BitsPerSecond{400'000'000'000ULL}),
              sim::SimDurationNs{20'972});
}

TEST(SerializationDelayTest, RejectsZeroBytesAndZeroBandwidth) {
    EXPECT_FALSE(serialization_delay(ByteCount{0}, BitsPerSecond{1}).has_value());
    EXPECT_FALSE(serialization_delay(ByteCount{1}, BitsPerSecond{0}).has_value());
}

TEST(SerializationDelayTest, RejectsResultsBeyondSimulatedTime) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    EXPECT_FALSE(serialization_delay(ByteCount{maximum}, BitsPerSecond{1}).has_value());
}

TEST(SerializationDelayTest, AcceptsMaximumExactlyRepresentableDelay) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    EXPECT_EQ(serialization_delay(ByteCount{maximum}, BitsPerSecond{8'000'000'000ULL}),
              sim::SimDurationNs{maximum});
}

TEST(SerializationDelayTest, HandlesMaximumBandwidthWithoutIntermediateOverflow) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    EXPECT_EQ(serialization_delay(ByteCount{maximum}, BitsPerSecond{maximum}),
              sim::SimDurationNs{8'000'000'000ULL});
    EXPECT_EQ(serialization_delay(ByteCount{1}, BitsPerSecond{maximum}), sim::SimDurationNs{1});
}

} // namespace
} // namespace nexuslab::transport
