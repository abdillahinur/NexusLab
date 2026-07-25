// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/graph.hpp"

#include <cstddef>
#include <memory>

namespace nexuslab::topology {

struct ClosConfig final {
    std::size_t gpu_count;
    std::size_t gpus_per_nic;
    std::size_t nics_per_leaf;
    std::size_t spine_count;

    bool operator==(const ClosConfig&) const = default;
};

struct ClosDimensions final {
    std::size_t gpu_count;
    std::size_t nic_count;
    std::size_t leaf_count;
    std::size_t spine_count;
    std::size_t rack_count;
    std::size_t switch_count;
    std::size_t port_count;
    std::size_t link_count;

    bool operator==(const ClosDimensions&) const = default;
};

[[nodiscard]] constexpr ClosConfig initial_clos_config() noexcept {
    return ClosConfig{512U, 8U, 8U, 8U};
}

[[nodiscard]] constexpr ClosConfig stretch_clos_config() noexcept {
    return ClosConfig{2'048U, 8U, 8U, 8U};
}

[[nodiscard]] ClosDimensions clos_dimensions(const ClosConfig& config);
[[nodiscard]] std::unique_ptr<TopologyGraph> generate_clos(const ClosConfig& config);

} // namespace nexuslab::topology
