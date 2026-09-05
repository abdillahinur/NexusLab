// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/cli/application.hpp"

#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/export.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/topology/summary.hpp"
#include "nexuslab/version.hpp"
#include "nexuslab/workload/run.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace nexuslab::cli {
namespace {

void print_usage(std::ostream& output) {
    output << "Usage:\n"
              "  nexuslab --help\n"
              "  nexuslab --version\n"
              "  nexuslab train --profiles\n"
              "  nexuslab train --file <scenario.yaml> [--timeline]\n"
              "  nexuslab topology summary --clos <initial|stretch>\n"
              "  nexuslab topology summary --file <topology.yaml>\n";
}

[[nodiscard]] std::string read_file(std::string_view path) {
    std::ifstream input{std::string{path}, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"cannot open topology file: " + std::string{path}};
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error{"cannot read topology file: " + std::string{path}};
    }
    return contents.str();
}

[[nodiscard]] std::string read_training_file(std::string_view path) {
    std::ifstream input{std::string{path}, std::ios::binary | std::ios::ate};
    if (!input || input.tellg() < 0 || input.tellg() > 1'048'576) {
        throw std::invalid_argument{"training file is unreadable or exceeds one MiB"};
    }
    std::string yaml(static_cast<std::size_t>(input.tellg()), '\0');
    input.seekg(0);
    input.read(yaml.data(), static_cast<std::streamsize>(yaml.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error{"training file changed while reading"};
    }
    return yaml;
}

[[nodiscard]] std::unique_ptr<topology::TopologyGraph> load_clos_profile(std::string_view profile) {
    if (profile == "initial") {
        return topology::generate_clos(topology::initial_clos_config());
    }
    if (profile == "stretch") {
        return topology::generate_clos(topology::stretch_clos_config());
    }
    throw std::invalid_argument{"unknown Clos profile: " + std::string{profile}};
}

[[nodiscard]] int run_topology_summary(std::span<const std::string_view> arguments,
                                       std::ostream& output) {
    if (arguments.size() != 4U) {
        return 2;
    }

    std::unique_ptr<topology::TopologyGraph> graph;
    std::string source;
    if (arguments[2] == "--clos") {
        graph = load_clos_profile(arguments[3]);
        source = "clos:" + std::string{arguments[3]};
    } else if (arguments[2] == "--file") {
        graph = topology::deserialize_topology_yaml(read_file(arguments[3]));
        source = "file:" + std::string{arguments[3]};
    } else {
        return 2;
    }

    output << topology::format_topology_summary(topology::summarize_topology(*graph), source);
    return 0;
}

} // namespace

int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    if (arguments.size() == 1U) {
        if (arguments[0] == "--version") {
            output << "NexusLab " << version() << '\n';
            return 0;
        }
        if (arguments[0] == "--help" || arguments[0] == "-h") {
            print_usage(output);
            return 0;
        }
    }

    if (arguments.size() == 2U && arguments[0] == "train" && arguments[1] == "--profiles") {
        for (const auto& profile : workload::profiles()) {
            output << profile.name << ": " << profile.assumption << '\n';
        }
        return 0;
    }
    if ((arguments.size() == 3U || arguments.size() == 4U) && arguments[0] == "train" &&
        arguments[1] == "--file") {
        if (arguments.size() == 4U && arguments[3] != "--timeline") {
            print_usage(error);
            return 2;
        }
        const auto yaml = read_training_file(arguments[2]);
        const auto report = workload::run_training(workload::parse_scenario(yaml));
        workload::write_report(report, output, arguments.size() == 4U);
        return std::any_of(
                   report.jobs.begin(), report.jobs.end(),
                   [](const auto& job) { return job.state != workload::JobState::Succeeded; })
                   ? 1
                   : 0;
    }

    if (arguments.size() >= 2U && arguments[0] == "topology" && arguments[1] == "summary") {
        const int status = run_topology_summary(arguments, output);
        if (status == 0) {
            return status;
        }
    }

    print_usage(error);
    return 2;
}

} // namespace nexuslab::cli
