// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/scheduling/policy.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace nexuslab::scheduling {
namespace {
std::uint64_t mix(std::uint64_t value) noexcept {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
Placement pinned(const JobRequest& request, ClusterResourceView view) {
    std::map<topology::GpuId, const Resource*> resources;
    for (const auto& gpu : view.gpus) {
        resources.emplace(gpu.gpu, &gpu);
    }
    std::set<topology::GpuId> unique;
    bool blocked{false};
    for (const auto id : request.pinned) {
        const auto found = resources.find(id);
        if (found == resources.end() || !unique.insert(id).second) {
            return {PlacementOutcome::Rejected, {}, "invalid pinned GPU list"};
        }
        blocked = blocked || !found->second->healthy || found->second->owner.has_value();
    }
    if (blocked) {
        return {PlacementOutcome::Waiting, {}, "pinned GPUs unavailable"};
    }
    return {PlacementOutcome::Placed,
            {request.pinned.begin(), request.pinned.end()},
            "pinned rank order"};
}
void compact(std::vector<const Resource*>& free) {
    std::map<topology::RackId, std::size_t> racks;
    std::map<topology::NicId, std::size_t> nics;
    for (const auto* gpu : free) {
        ++racks[gpu->rack];
        ++nics[gpu->nic];
    }
    std::sort(free.begin(), free.end(), [&](const auto* a, const auto* b) {
        if (a->rack != b->rack) {
            return racks.at(a->rack) != racks.at(b->rack) ? racks.at(a->rack) > racks.at(b->rack)
                                                          : a->rack < b->rack;
        }
        if (a->nic != b->nic) {
            return nics.at(a->nic) != nics.at(b->nic) ? nics.at(a->nic) > nics.at(b->nic)
                                                      : a->nic < b->nic;
        }
        return a->gpu < b->gpu;
    });
}
void rack_local(std::vector<const Resource*>& free, std::uint32_t count) {
    std::map<topology::RackId, std::size_t> racks;
    for (const auto* gpu : free) {
        ++racks[gpu->rack];
    }
    const auto found = std::find_if(racks.begin(), racks.end(),
                                    [&](const auto& rack) { return rack.second >= count; });
    if (found != racks.end()) {
        std::stable_partition(free.begin(), free.end(),
                              [&](const auto* gpu) { return gpu->rack == found->first; });
    }
}
class BuiltinPolicy final : public SchedulingPolicy {
  public:
    BuiltinPolicy(std::string name, std::uint64_t seed) : name_{std::move(name)}, seed_{seed} {}
    [[nodiscard]] Placement place(const JobRequest& request,
                                  ClusterResourceView resources) const override {
        if (request.workers == 0 || request.workers > resources.gpus.size() ||
            (!request.pinned.empty() && request.pinned.size() != request.workers)) {
            return {PlacementOutcome::Rejected,
                    {},
                    "request exceeds physical capacity or has invalid dimensions"};
        }
        if (!request.pinned.empty()) {
            return pinned(request, resources);
        }
        std::vector<const Resource*> free;
        for (const auto& gpu : resources.gpus) {
            if (gpu.healthy && !gpu.owner.has_value()) {
                free.push_back(&gpu);
            }
        }
        if (free.size() < request.workers) {
            return {PlacementOutcome::Waiting, {}, "insufficient free healthy GPUs"};
        }
        order(free, request);
        std::vector<topology::GpuId> workers;
        for (std::size_t index = 0; index < request.workers; ++index) {
            workers.push_back(free[index]->gpu);
        }
        return {PlacementOutcome::Placed, std::move(workers), name_ + " deterministic rank order"};
    }

  private:
    void order(std::vector<const Resource*>& free, const JobRequest& request) const {
        std::sort(free.begin(), free.end(),
                  [](const auto* a, const auto* b) { return a->gpu < b->gpu; });
        if (name_ == "random") {
            const auto score = [&](auto id) {
                return mix(seed_ ^ mix(request.job.value() + 0x9e3779b97f4a7c15ULL) ^
                           mix(id.value()));
            };
            std::sort(free.begin(), free.end(), [&](const auto* a, const auto* b) {
                const auto left = score(a->gpu);
                const auto right = score(b->gpu);
                return left != right ? left < right : a->gpu < b->gpu;
            });
        } else if (name_ == "rack-local") {
            rack_local(free, request.workers);
        } else if (name_ == "compact") {
            compact(free);
        }
    }
    std::string name_;
    std::uint64_t seed_;
};
} // namespace
std::unique_ptr<SchedulingPolicy> make_policy(std::string_view name, std::uint64_t seed) {
    if (name != "first-fit" && name != "random" && name != "rack-local" && name != "compact") {
        throw std::invalid_argument{"unknown scheduling policy"};
    }
    return std::make_unique<BuiltinPolicy>(std::string{name}, seed);
}
} // namespace nexuslab::scheduling
