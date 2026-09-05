// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/scheduling/policy.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace nexuslab::scheduling {
ResourceInventory::ResourceInventory(const topology::TopologyGraph& graph) {
    if (graph.gpus().empty() || graph.gpus().size() > 8192) {
        throw std::length_error{"scheduler inventory requires 1..8192 GPUs"};
    }
    for (const auto& gpu : graph.gpus()) {
        gpus_.push_back({gpu.id, gpu.rack, gpu.attached_nic, true, std::nullopt});
    }
    std::sort(gpus_.begin(), gpus_.end(),
              [](const auto& a, const auto& b) { return a.gpu < b.gpu; });
}
ClusterResourceView ResourceInventory::view() const noexcept { return {gpus_}; }
void ResourceInventory::allocate(workload::JobId job, std::span<const topology::GpuId> workers) {
    if (workers.empty() || workers.size() > gpus_.size() ||
        std::any_of(gpus_.begin(), gpus_.end(), [&](const auto& r) { return r.owner == job; })) {
        throw std::invalid_argument{"invalid or duplicate allocation"};
    }
    std::set<topology::GpuId> unique;
    std::vector<std::size_t> indices;
    for (const auto id : workers) {
        const auto found = std::lower_bound(gpus_.begin(), gpus_.end(), id,
                                            [](const auto& r, auto gpu) { return r.gpu < gpu; });
        if (found == gpus_.end() || found->gpu != id || !found->healthy ||
            found->owner.has_value() || !unique.insert(id).second) {
            throw std::invalid_argument{"placement contains unavailable or duplicate GPU"};
        }
        indices.push_back(static_cast<std::size_t>(found - gpus_.begin()));
    }
    for (const auto index : indices) {
        gpus_[index].owner = job;
    }
}
void ResourceInventory::release(workload::JobId job) noexcept {
    for (auto& gpu : gpus_) {
        if (gpu.owner == job) {
            gpu.owner.reset();
        }
    }
}
std::optional<workload::JobId> ResourceInventory::set_health(topology::GpuId gpu, bool healthy) {
    const auto found = std::lower_bound(gpus_.begin(), gpus_.end(), gpu,
                                        [](const auto& r, auto id) { return r.gpu < id; });
    if (found == gpus_.end() || found->gpu != gpu) {
        throw std::invalid_argument{"unknown scheduler GPU"};
    }
    found->healthy = healthy;
    return found->owner;
}
std::size_t fragmentation(ClusterResourceView resources) {
    std::map<topology::RackId, std::size_t> free;
    std::size_t total{0};
    std::size_t largest{0};
    for (const auto& gpu : resources.gpus) {
        if (gpu.healthy && !gpu.owner.has_value()) {
            ++total;
            largest = std::max(largest, ++free[gpu.rack]);
        }
    }
    return total - largest;
}
Locality locality(ClusterResourceView resources, std::span<const topology::GpuId> workers) {
    std::map<topology::GpuId, const Resource*> lookup;
    for (const auto& gpu : resources.gpus) {
        lookup.emplace(gpu.gpu, &gpu);
    }
    std::set<topology::RackId> racks;
    std::set<topology::NicId> nics;
    Locality result;
    for (std::size_t index = 0; index < workers.size(); ++index) {
        const auto& gpu = *lookup.at(workers[index]);
        racks.insert(gpu.rack);
        nics.insert(gpu.nic);
        if (gpu.rack != lookup.at(workers[(index + 1) % workers.size()])->rack) {
            ++result.cross_rack_ring_edges;
        }
    }
    result.racks = racks.size();
    result.nics = nics.size();
    return result;
}
} // namespace nexuslab::scheduling
