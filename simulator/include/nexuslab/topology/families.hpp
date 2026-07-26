// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/graph.hpp"

#include <cstddef>
#include <memory>

namespace nexuslab::topology {

struct SingleRackConfig final {
    std::size_t gpu_count;
    std::size_t gpus_per_nic;

    bool operator==(const SingleRackConfig&) const = default;
};

struct LeafSpineConfig final {
    std::size_t leaf_count;
    std::size_t nics_per_leaf;
    std::size_t gpus_per_nic;
    std::size_t spine_count;

    bool operator==(const LeafSpineConfig&) const = default;
};

[[nodiscard]] std::unique_ptr<TopologyGraph> generate_two_gpu_direct();
[[nodiscard]] std::unique_ptr<TopologyGraph> generate_single_rack(const SingleRackConfig& config);
[[nodiscard]] std::unique_ptr<TopologyGraph> generate_leaf_spine(const LeafSpineConfig& config);

} // namespace nexuslab::topology
