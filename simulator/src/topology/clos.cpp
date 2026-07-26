// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/clos.hpp"

#include "nexuslab/topology/families.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace nexuslab::topology {
namespace {

[[nodiscard]] std::size_t checked_add(std::size_t left, std::size_t right) {
    if (std::numeric_limits<std::size_t>::max() - left < right) {
        throw std::overflow_error{"Clos topology entity count overflow"};
    }
    return left + right;
}

[[nodiscard]] std::size_t checked_multiply(std::size_t left, std::size_t right) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::overflow_error{"Clos topology entity count overflow"};
    }
    return left * right;
}

void require_positive(std::size_t value, const char* field_name) {
    if (value == 0U) {
        throw std::invalid_argument{std::string{"Clos "} + field_name + " must be positive"};
    }
}

} // namespace

ClosDimensions clos_dimensions(const ClosConfig& config) {
    require_positive(config.gpu_count, "GPU count");
    require_positive(config.gpus_per_nic, "GPUs per NIC");
    require_positive(config.nics_per_leaf, "NICs per leaf");
    require_positive(config.spine_count, "spine count");

    if (config.gpu_count % config.gpus_per_nic != 0U) {
        throw std::invalid_argument{"Clos GPU count must be divisible by GPUs per NIC"};
    }
    const std::size_t nic_count = config.gpu_count / config.gpus_per_nic;
    if (nic_count % config.nics_per_leaf != 0U) {
        throw std::invalid_argument{"Clos NIC count must be divisible by NICs per leaf"};
    }

    const std::size_t leaf_count = nic_count / config.nics_per_leaf;
    const std::size_t leaf_spine_links = checked_multiply(leaf_count, config.spine_count);
    const std::size_t switch_count = checked_add(leaf_count, config.spine_count);
    const std::size_t link_count =
        checked_add(checked_add(config.gpu_count, nic_count), leaf_spine_links);
    const std::size_t port_count = checked_multiply(link_count, 2U);
    return ClosDimensions{config.gpu_count, nic_count,    leaf_count, config.spine_count,
                          leaf_count,       switch_count, port_count, link_count};
}

std::unique_ptr<TopologyGraph> generate_clos(const ClosConfig& config) {
    const ClosDimensions dimensions = clos_dimensions(config);
    return generate_leaf_spine(LeafSpineConfig{dimensions.leaf_count, config.nics_per_leaf,
                                               config.gpus_per_nic, dimensions.spine_count});
}

} // namespace nexuslab::topology
