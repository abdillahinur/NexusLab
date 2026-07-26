// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/cli/application.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::string_view> arguments(int argc, char** argv) {
    std::vector<std::string_view> result;
    result.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
        result.emplace_back(argv[index]);
    }
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return nexuslab::cli::run(arguments(argc, argv), std::cout, std::cerr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "nexuslab failed: %s\n", error.what());
    } catch (...) {
        std::fputs("nexuslab failed: unknown error\n", stderr);
    }
    return 1;
}
