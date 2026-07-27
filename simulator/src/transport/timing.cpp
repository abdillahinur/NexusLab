// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/timing.hpp"

#include <cstdint>
#include <limits>

namespace nexuslab::transport {
namespace {

constexpr std::uint64_t bit_nanoseconds_per_byte = 8'000'000'000ULL;

struct DivisionResult final {
    std::uint64_t quotient;
    std::uint64_t remainder;
};

struct FractionProduct final {
    std::uint64_t numerator;
    std::uint64_t multiplier;
    std::uint64_t denominator;
};

[[nodiscard]] constexpr DivisionResult multiply_fraction(FractionProduct product) noexcept {
    std::uint64_t quotient{0};
    std::uint64_t remainder{0};

    for (int bit = 63; bit >= 0; --bit) {
        quotient *= 2;
        if (remainder >= product.denominator - remainder) {
            remainder -= product.denominator - remainder;
            ++quotient;
        } else {
            remainder *= 2;
        }

        if (((product.multiplier >> bit) & 1U) == 0U) {
            continue;
        }

        if (remainder >= product.denominator - product.numerator) {
            remainder -= product.denominator - product.numerator;
            ++quotient;
        } else {
            remainder += product.numerator;
        }
    }

    return DivisionResult{quotient, remainder};
}

} // namespace

std::optional<sim::SimDurationNs> serialization_delay(ByteCount bytes,
                                                      BitsPerSecond bandwidth) noexcept {
    const std::uint64_t byte_count = bytes.value();
    const std::uint64_t bits_per_second = bandwidth.value();
    if (byte_count == 0 || bits_per_second == 0) {
        return std::nullopt;
    }

    const std::uint64_t whole_seconds = byte_count / bits_per_second;
    const std::uint64_t fractional_bytes = byte_count % bits_per_second;
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();

    if (whole_seconds > maximum / bit_nanoseconds_per_byte) {
        return std::nullopt;
    }
    std::uint64_t nanoseconds = whole_seconds * bit_nanoseconds_per_byte;

    const DivisionResult fractional = multiply_fraction(
        FractionProduct{fractional_bytes, bit_nanoseconds_per_byte, bits_per_second});
    if (fractional.quotient > maximum - nanoseconds) {
        return std::nullopt;
    }
    nanoseconds += fractional.quotient;

    if (fractional.remainder != 0) {
        if (nanoseconds == maximum) {
            return std::nullopt;
        }
        ++nanoseconds;
    }

    return sim::SimDurationNs{nanoseconds};
}

} // namespace nexuslab::transport
