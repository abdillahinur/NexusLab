// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nexuslab/topology/graph.hpp"
#include <deque>
#include <map>
#include <vector>

namespace nexuslab::routing {
using Path = std::vector<topology::DirectedLinkId>;
struct Endpoints final {
    topology::NodeId source;
    topology::NodeId destination;
    auto operator<=>(const Endpoints&) const = default;
};
struct PathLimits final {
    std::size_t maximum_paths{64};
    std::size_t maximum_hops{64};
    std::size_t maximum_cached_pairs{1'024};
    std::size_t maximum_cached_route_entries{262'144};
};
struct CacheStatistics final {
    std::uint64_t hits{0};
    std::uint64_t misses{0};
    std::uint64_t invalidations{0};
    std::size_t pairs{0};
    std::size_t paths{0};
    std::size_t route_entries{0};
};
class PathService final {
  public:
    explicit PathService(const topology::TopologyGraph& graph, PathLimits limits = {});
    // Borrowed result is valid until the next lookup; graph structure must remain fixed.
    [[nodiscard]] const std::vector<Path>& lookup(Endpoints endpoints);
    [[nodiscard]] CacheStatistics statistics() const noexcept;

  private:
    [[nodiscard]] std::size_t index(topology::NodeId node) const;
    [[nodiscard]] std::vector<Path> enumerate(Endpoints endpoints) const;
    void walk(topology::NodeId node, const std::vector<std::size_t>& distance, Path& path,
              std::vector<Path>& result) const;
    void evict_oldest();
    const topology::TopologyGraph* graph_;
    PathLimits limits_;
    std::uint64_t revision_;
    CacheStatistics statistics_;
    std::map<Endpoints, std::vector<Path>> cache_;
    std::deque<Endpoints> order_;
};
} // namespace nexuslab::routing
