// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint64_t default_iterations = 1'000'000;

[[nodiscard]] bool parse_iterations(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}

void print_usage(std::ostream& output) {
    output << "Usage: nexuslab_benchmarks [--iterations COUNT]\n";
}

int run(int argc, char** argv) {
    std::uint64_t iterations = default_iterations;

    if (argc == 3 && std::string_view{argv[1]} == "--iterations") {
        if (!parse_iterations(argv[2], iterations)) {
            std::cerr << "iterations must be a positive unsigned integer\n";
            return 2;
        }
    } else if (argc != 1) {
        print_usage(std::cerr);
        return 2;
    }

    std::uint64_t checksum = 0;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < iterations; ++index) {
        checksum ^= index + 0x9e3779b97f4a7c15ULL + (checksum << 6U) + (checksum >> 2U);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    std::cout << "benchmark=foundation_harness\n"
              << "iterations=" << iterations << '\n'
              << "elapsed_ns=" << elapsed_ns << '\n'
              << "checksum=" << checksum << '\n'
              << "scope=benchmark_harness_only_not_simulation_performance\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "benchmark failed: %s\n", error.what());
    } catch (...) {
        std::fputs("benchmark failed: unknown error\n", stderr);
    }
    return 1;
}
