// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/graph.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace nexuslab::topology {

inline constexpr std::uint32_t topology_yaml_schema_version = 1U;

class TopologyYamlError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string serialize_topology_yaml(const TopologyGraph& graph);
[[nodiscard]] std::unique_ptr<TopologyGraph> deserialize_topology_yaml(std::string_view yaml);
[[nodiscard]] std::string export_topology_dot(const TopologyGraph& graph);

} // namespace nexuslab::topology
