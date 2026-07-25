// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/sim/deterministic_rng.hpp"
#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/sim/time.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nexuslab::sim {
namespace {

TEST(SimTimeTest, ConvertsSupportedUnitsExactly) {
    const auto nanoseconds = checked_duration_from(7, SimTimeUnit::Nanoseconds);
    const auto microseconds = checked_duration_from(7, SimTimeUnit::Microseconds);
    const auto milliseconds = checked_duration_from(7, SimTimeUnit::Milliseconds);
    const auto seconds = checked_duration_from(7, SimTimeUnit::Seconds);

    EXPECT_EQ(nanoseconds, SimDurationNs{7});
    EXPECT_EQ(microseconds, SimDurationNs{7'000});
    EXPECT_EQ(milliseconds, SimDurationNs{7'000'000});
    EXPECT_EQ(seconds, SimDurationNs{7'000'000'000});
}

TEST(SimTimeTest, RejectsUnitConversionOverflow) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    EXPECT_FALSE(checked_duration_from(maximum, SimTimeUnit::Microseconds).has_value());
    EXPECT_FALSE(checked_duration_from(maximum, SimTimeUnit::Milliseconds).has_value());
    EXPECT_FALSE(checked_duration_from(maximum, SimTimeUnit::Seconds).has_value());
}

TEST(SimTimeTest, AddsDurationWithoutLosingTypeInformation) {
    const auto result = checked_add(SimTimeNs{500}, SimDurationNs{250});

    EXPECT_EQ(result, SimTimeNs{750});
}

TEST(SimTimeTest, RejectsTimestampOverflow) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

    EXPECT_FALSE(checked_add(SimTimeNs{maximum}, SimDurationNs{1}).has_value());
}

TEST(SimTimeTest, ComputesOnlyNonnegativeDifferences) {
    const auto forward = checked_difference(SimTimeNs{900}, SimTimeNs{400});
    const auto backward = checked_difference(SimTimeNs{400}, SimTimeNs{900});

    EXPECT_EQ(forward, SimDurationNs{500});
    EXPECT_FALSE(backward.has_value());
}

TEST(EventIdTest, IsStronglyTypedAndOrdered) {
    constexpr EventId first{10};
    constexpr EventId second{11};

    EXPECT_LT(first, second);
    EXPECT_EQ(first.value(), 10U);
}

TEST(DeterministicRngTest, MatchesMt19937_64GoldenVector) {
    DeterministicRng rng{42};
    constexpr std::array<std::uint64_t, 5> expected{
        13'930'160'852'258'120'406ULL, 11'788'048'577'503'494'824ULL, 13'874'630'024'467'741'450ULL,
        2'513'787'319'205'155'662ULL,  16'662'371'453'428'439'381ULL,
    };

    for (const auto value : expected) {
        EXPECT_EQ(rng.next_u64(), value);
    }
    EXPECT_EQ(rng.draw_count(), expected.size());
}

TEST(DeterministicRngTest, ProducesStableBoundedValues) {
    DeterministicRng rng{42};
    constexpr std::array<std::uint64_t, 5> expected{6, 4, 0, 2, 1};

    for (const auto value : expected) {
        EXPECT_EQ(rng.uniform_below(10), value);
    }
    EXPECT_EQ(rng.draw_count(), expected.size());
}

TEST(DeterministicRngTest, RejectsZeroBoundWithoutDrawing) {
    DeterministicRng rng{42};

    EXPECT_THROW(static_cast<void>(rng.uniform_below(0)), std::invalid_argument);
    EXPECT_EQ(rng.draw_count(), 0U);
}

TEST(DeterministicRngTest, CopiesStateForDeterministicSnapshots) {
    DeterministicRng original{42};
    static_cast<void>(original.next_u64());
    DeterministicRng snapshot = original;

    EXPECT_EQ(snapshot.draw_count(), original.draw_count());
    EXPECT_EQ(snapshot.next_u64(), original.next_u64());
    EXPECT_EQ(snapshot.draw_count(), original.draw_count());
}

} // namespace
} // namespace nexuslab::sim
