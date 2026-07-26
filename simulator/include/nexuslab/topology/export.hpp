// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/graph.hpp"

#include <cstdint>
#include <string>

namespace nexuslab::topology {

inline constexpr std::uint32_t topology_yaml_schema_version = 1U;

[[nodiscard]] std::string serialize_topology_yaml(const TopologyGraph& graph);
[[nodiscard]] std::string export_topology_dot(const TopologyGraph& graph);

} // namespace nexuslab::topology
