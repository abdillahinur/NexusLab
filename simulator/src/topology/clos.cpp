// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/clos.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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
    auto graph = std::make_unique<TopologyGraph>();
    std::vector<SwitchId> leaves;
    leaves.reserve(dimensions.leaf_count);

    for (std::size_t leaf_index = 0; leaf_index < dimensions.leaf_count; ++leaf_index) {
        const RackId rack = graph->add_rack();
        const SwitchId leaf = graph->add_leaf_switch(rack);
        leaves.push_back(leaf);
        for (std::size_t nic_index = 0; nic_index < config.nics_per_leaf; ++nic_index) {
            const NicId nic = graph->add_nic(rack);
            for (std::size_t gpu_index = 0; gpu_index < config.gpus_per_nic; ++gpu_index) {
                static_cast<void>(graph->add_gpu(nic));
            }
            static_cast<void>(graph->connect_fabric(NodeId{nic}, PortRole::FabricUplink,
                                                    NodeId{leaf}, PortRole::FabricDownlink));
        }
    }

    std::vector<SwitchId> spines;
    spines.reserve(dimensions.spine_count);
    for (std::size_t spine_index = 0; spine_index < dimensions.spine_count; ++spine_index) {
        spines.push_back(graph->add_spine_switch());
    }
    for (SwitchId leaf : leaves) {
        for (SwitchId spine : spines) {
            static_cast<void>(graph->connect_fabric(NodeId{leaf}, PortRole::FabricUplink,
                                                    NodeId{spine}, PortRole::FabricDownlink));
        }
    }

    return graph;
}

} // namespace nexuslab::topology
