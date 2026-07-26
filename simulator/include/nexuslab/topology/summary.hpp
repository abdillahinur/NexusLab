// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/graph.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace nexuslab::topology {

struct TopologySummary final {
    std::size_t racks;
    std::size_t gpus;
    std::size_t nics;
    std::size_t switches;
    std::size_t leaf_switches;
    std::size_t spine_switches;
    std::size_t ports;
    std::size_t links;
    std::size_t local_links;
    std::size_t fabric_links;
    std::size_t down_switches;
    std::size_t down_ports;
    std::size_t down_links;

    bool operator==(const TopologySummary&) const = default;
};

[[nodiscard]] TopologySummary summarize_topology(const TopologyGraph& graph) noexcept;
[[nodiscard]] std::string format_topology_summary(const TopologySummary& summary,
                                                  std::string_view source);

} // namespace nexuslab::topology
