// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/families.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace nexuslab::topology {
namespace {

struct LeafRackDimensions final {
    std::size_t nic_count;
    std::size_t gpus_per_nic;
};

[[nodiscard]] std::size_t checked_add(std::size_t left, std::size_t right,
                                      const char* topology_name) {
    if (std::numeric_limits<std::size_t>::max() - left < right) {
        throw std::overflow_error{std::string{topology_name} + " entity count overflow"};
    }
    return left + right;
}

[[nodiscard]] std::size_t checked_multiply(std::size_t left, std::size_t right,
                                           const char* topology_name) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::overflow_error{std::string{topology_name} + " entity count overflow"};
    }
    return left * right;
}

void require_positive(std::size_t value, const char* topology_name, const char* field_name) {
    if (value == 0U) {
        throw std::invalid_argument{std::string{topology_name} + ' ' + field_name +
                                    " must be positive"};
    }
}

void validate_single_rack_config(const SingleRackConfig& config) {
    require_positive(config.gpu_count, "single-rack", "GPU count");
    require_positive(config.gpus_per_nic, "single-rack", "GPUs per NIC");
    if (config.gpu_count % config.gpus_per_nic != 0U) {
        throw std::invalid_argument{"single-rack GPU count must be divisible by GPUs per NIC"};
    }

    const std::size_t nic_count = config.gpu_count / config.gpus_per_nic;
    const std::size_t link_count = checked_add(config.gpu_count, nic_count, "single-rack");
    static_cast<void>(checked_multiply(link_count, 2U, "single-rack"));
}

void validate_leaf_spine_config(const LeafSpineConfig& config) {
    require_positive(config.leaf_count, "leaf-spine", "leaf count");
    require_positive(config.nics_per_leaf, "leaf-spine", "NICs per leaf");
    require_positive(config.gpus_per_nic, "leaf-spine", "GPUs per NIC");
    require_positive(config.spine_count, "leaf-spine", "spine count");

    const std::size_t nic_count =
        checked_multiply(config.leaf_count, config.nics_per_leaf, "leaf-spine");
    const std::size_t gpu_count = checked_multiply(nic_count, config.gpus_per_nic, "leaf-spine");
    const std::size_t leaf_spine_links =
        checked_multiply(config.leaf_count, config.spine_count, "leaf-spine");
    const std::size_t attachment_links = checked_add(gpu_count, nic_count, "leaf-spine");
    const std::size_t link_count = checked_add(attachment_links, leaf_spine_links, "leaf-spine");
    static_cast<void>(checked_add(config.leaf_count, config.spine_count, "leaf-spine"));
    static_cast<void>(checked_multiply(link_count, 2U, "leaf-spine"));
}

[[nodiscard]] SwitchId add_leaf_rack(TopologyGraph& graph, const LeafRackDimensions& dimensions) {
    const RackId rack = graph.add_rack();
    const SwitchId leaf = graph.add_leaf_switch(rack);
    for (std::size_t nic_index = 0; nic_index < dimensions.nic_count; ++nic_index) {
        const NicId nic = graph.add_nic(rack);
        for (std::size_t gpu_index = 0; gpu_index < dimensions.gpus_per_nic; ++gpu_index) {
            static_cast<void>(graph.add_gpu(nic));
        }
        static_cast<void>(graph.connect_fabric(NodeId{nic}, PortRole::FabricUplink, NodeId{leaf},
                                               PortRole::FabricDownlink));
    }
    return leaf;
}

} // namespace

std::unique_ptr<TopologyGraph> generate_two_gpu_direct() {
    auto graph = std::make_unique<TopologyGraph>();
    std::vector<NicId> nics;
    nics.reserve(2U);

    for (std::size_t endpoint_index = 0; endpoint_index < 2U; ++endpoint_index) {
        const RackId rack = graph->add_rack();
        const NicId nic = graph->add_nic(rack);
        static_cast<void>(graph->add_gpu(nic));
        nics.push_back(nic);
    }

    static_cast<void>(graph->connect_fabric(NodeId{nics[0]}, PortRole::FabricUplink,
                                            NodeId{nics[1]}, PortRole::FabricUplink));
    return graph;
}

std::unique_ptr<TopologyGraph> generate_single_rack(const SingleRackConfig& config) {
    validate_single_rack_config(config);
    auto graph = std::make_unique<TopologyGraph>();
    static_cast<void>(add_leaf_rack(
        *graph, LeafRackDimensions{config.gpu_count / config.gpus_per_nic, config.gpus_per_nic}));
    return graph;
}

std::unique_ptr<TopologyGraph> generate_leaf_spine(const LeafSpineConfig& config) {
    validate_leaf_spine_config(config);
    auto graph = std::make_unique<TopologyGraph>();
    std::vector<SwitchId> leaves;
    leaves.reserve(config.leaf_count);
    for (std::size_t leaf_index = 0; leaf_index < config.leaf_count; ++leaf_index) {
        leaves.push_back(
            add_leaf_rack(*graph, LeafRackDimensions{config.nics_per_leaf, config.gpus_per_nic}));
    }

    std::vector<SwitchId> spines;
    spines.reserve(config.spine_count);
    for (std::size_t spine_index = 0; spine_index < config.spine_count; ++spine_index) {
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
