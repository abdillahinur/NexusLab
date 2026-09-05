// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/sim/time.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/workload/events.hpp"
#include <memory>
#include <span>
#include <string>
namespace nexuslab::scheduling {
struct Resource final {
    topology::GpuId gpu;
    topology::RackId rack;
    topology::NicId nic;
    bool healthy{true};
    std::optional<workload::JobId> owner;
    bool operator==(const Resource&) const = default;
};
struct ClusterResourceView final {
    std::span<const Resource> gpus;
};
class ResourceInventory final {
  public:
    explicit ResourceInventory(const topology::TopologyGraph& graph);
    [[nodiscard]] ClusterResourceView view() const noexcept;
    void allocate(workload::JobId job, std::span<const topology::GpuId> workers);
    void release(workload::JobId job) noexcept;
    [[nodiscard]] std::optional<workload::JobId> set_health(topology::GpuId gpu, bool healthy);

  private:
    std::vector<Resource> gpus_;
};
struct JobRequest final {
    workload::JobId job;
    std::uint32_t workers;
    std::uint32_t priority;
    std::span<const topology::GpuId> pinned;
};
enum class PlacementOutcome : std::uint8_t { Placed = 1, Waiting = 2, Rejected = 3 };
struct Placement final {
    PlacementOutcome outcome;
    std::vector<topology::GpuId> workers;
    std::string reason;
};
class SchedulingPolicy {
  public:
    virtual ~SchedulingPolicy() = default;
    [[nodiscard]] virtual Placement place(const JobRequest& request,
                                          ClusterResourceView resources) const = 0;
};
struct Configuration final {
    std::string policy{"first-fit"};
    std::uint64_t seed{42};
    std::size_t decision_entries{1'000'000};
    std::size_t allocation_entries{1'000'000};
};
struct Locality final {
    std::size_t racks{0};
    std::size_t nics{0};
    std::size_t cross_rack_ring_edges{0};
    bool operator==(const Locality&) const = default;
};
struct PlacementDecision final {
    workload::JobId job;
    sim::SimTimeNs timestamp;
    std::string policy;
    std::uint32_t version;
    std::uint32_t priority;
    std::uint32_t requested_workers;
    PlacementOutcome outcome;
    std::vector<topology::GpuId> workers;
    std::string reason;
    Locality locality;
    std::size_t fragmentation_before;
    std::size_t fragmentation_after;
    bool operator==(const PlacementDecision&) const = default;
};
[[nodiscard]] std::unique_ptr<SchedulingPolicy> make_policy(std::string_view name,
                                                            std::uint64_t seed);
[[nodiscard]] std::size_t fragmentation(ClusterResourceView resources);
[[nodiscard]] Locality locality(ClusterResourceView resources,
                                std::span<const topology::GpuId> workers);
} // namespace nexuslab::scheduling
