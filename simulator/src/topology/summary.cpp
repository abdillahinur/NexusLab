// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/summary.hpp"

#include "nexuslab/topology/entities.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace nexuslab::topology {
namespace {

template <typename Entity>
[[nodiscard]] std::size_t count_down(std::span<const Entity> entities) noexcept {
    return static_cast<std::size_t>(
        std::ranges::count(entities, OperationalState::Down, &Entity::state));
}

} // namespace

TopologySummary summarize_topology(const TopologyGraph& graph) noexcept {
    const auto switches = graph.switches();
    const auto links = graph.links();
    const auto leaf_switches =
        static_cast<std::size_t>(std::ranges::count(switches, SwitchRole::Leaf, &Switch::role));
    const auto local_links = static_cast<std::size_t>(
        std::ranges::count(links, LinkKind::LocalAttachment, &PhysicalLink::kind));

    return TopologySummary{
        .racks = graph.racks().size(),
        .gpus = graph.gpus().size(),
        .nics = graph.nics().size(),
        .switches = switches.size(),
        .leaf_switches = leaf_switches,
        .spine_switches = switches.size() - leaf_switches,
        .ports = graph.ports().size(),
        .links = links.size(),
        .local_links = local_links,
        .fabric_links = links.size() - local_links,
        .down_switches = count_down(switches),
        .down_ports = count_down(graph.ports()),
        .down_links = count_down(links),
    };
}

std::string format_topology_summary(const TopologySummary& summary, std::string_view source) {
    std::ostringstream output;
    output << "source=" << source << '\n'
           << "racks=" << summary.racks << '\n'
           << "gpus=" << summary.gpus << '\n'
           << "nics=" << summary.nics << '\n'
           << "switches=" << summary.switches << '\n'
           << "leaf_switches=" << summary.leaf_switches << '\n'
           << "spine_switches=" << summary.spine_switches << '\n'
           << "ports=" << summary.ports << '\n'
           << "links=" << summary.links << '\n'
           << "local_links=" << summary.local_links << '\n'
           << "fabric_links=" << summary.fabric_links << '\n'
           << "down_switches=" << summary.down_switches << '\n'
           << "down_ports=" << summary.down_ports << '\n'
           << "down_links=" << summary.down_links << '\n';
    return output.str();
}

} // namespace nexuslab::topology
