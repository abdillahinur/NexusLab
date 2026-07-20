// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/version.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

void print_usage(std::ostream& output) { output << "Usage: nexuslab [--help | --version]\n"; }

int run(int argc, char** argv) {
    if (argc == 2) {
        const std::string_view argument{argv[1]};
        if (argument == "--version") {
            std::cout << "NexusLab " << nexuslab::version() << '\n';
            return 0;
        }
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }
    }

    print_usage(std::cerr);
    return 2;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "nexuslab failed: %s\n", error.what());
    } catch (...) {
        std::fputs("nexuslab failed: unknown error\n", stderr);
    }
    return 1;
}
