// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/routing/paths.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nexuslab::routing {
PathService::PathService(const topology::TopologyGraph& graph, PathLimits limits)
    : graph_{&graph}, limits_{limits}, revision_{graph.operational_revision()} {
    if (limits.maximum_paths == 0 || limits.maximum_hops == 0 || limits.maximum_hops > 1'024 ||
        limits.maximum_cached_pairs == 0 || limits.maximum_cached_route_entries == 0) {
        throw std::invalid_argument{"invalid routing path limits"};
    }
}
std::size_t PathService::index(topology::NodeId node) const {
    if (node.kind() == topology::NodeKind::Nic &&
        graph_->find(topology::NicId{node.value()}) != nullptr) {
        return static_cast<std::size_t>(node.value());
    }
    if (node.kind() == topology::NodeKind::Switch &&
        graph_->find(topology::SwitchId{node.value()}) != nullptr) {
        return graph_->nics().size() + static_cast<std::size_t>(node.value());
    }
    throw std::invalid_argument{"routing endpoint must be a known NIC or switch"};
}
std::vector<Path> PathService::enumerate(Endpoints endpoints) const {
    const auto source = index(endpoints.source);
    const auto destination = index(endpoints.destination);
    if (source == destination) {
        throw std::invalid_argument{"routing requires distinct endpoints"};
    }
    constexpr auto unreachable = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> distance(graph_->nics().size() + graph_->switches().size(),
                                      unreachable);
    distance[destination] = 0;
    std::vector<topology::NodeId> frontier{endpoints.destination};
    for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
        const auto node = frontier[cursor];
        for (const auto& arc : graph_->outgoing(node)) {
            if (graph_->find(arc.id.link)->kind != topology::LinkKind::Fabric ||
                !graph_->is_operational(arc)) {
                continue;
            }
            const auto neighbor = graph_->find(arc.destination)->owner;
            auto& next = distance[index(neighbor)];
            if (next == unreachable) {
                next = distance[index(node)] + 1;
                frontier.push_back(neighbor);
            }
        }
    }
    if (distance[source] == unreachable) {
        return {};
    }
    if (distance[source] > limits_.maximum_hops) {
        throw std::length_error{"shortest route exceeds hop limit"};
    }
    Path path;
    std::vector<Path> result;
    walk(endpoints.source, distance, path, result);
    std::ranges::sort(result);
    return result;
}
void PathService::walk(topology::NodeId node, const std::vector<std::size_t>& distance, Path& path,
                       std::vector<Path>& result) const {
    if (distance[index(node)] == 0) {
        if (result.size() == limits_.maximum_paths ||
            result.size() >= limits_.maximum_cached_route_entries / path.size()) {
            throw std::length_error{"complete shortest-path set exceeds routing limits"};
        }
        result.push_back(path);
        return;
    }
    for (const auto& arc : graph_->outgoing(node)) {
        if (graph_->find(arc.id.link)->kind != topology::LinkKind::Fabric ||
            !graph_->is_operational(arc)) {
            continue;
        }
        const auto neighbor = graph_->find(arc.destination)->owner;
        if (distance[index(neighbor)] < distance[index(node)]) {
            path.push_back(arc.id);
            walk(neighbor, distance, path, result);
            path.pop_back();
        }
    }
}
void PathService::evict_oldest() {
    const auto oldest = cache_.find(order_.front());
    for (const auto& path : oldest->second) {
        statistics_.route_entries -= path.size();
    }
    statistics_.paths -= oldest->second.size();
    cache_.erase(oldest);
    order_.pop_front();
}
const std::vector<Path>& PathService::lookup(Endpoints endpoints) {
    if (revision_ != graph_->operational_revision()) {
        cache_.clear();
        order_.clear();
        statistics_.paths = 0;
        statistics_.route_entries = 0;
        ++statistics_.invalidations;
        revision_ = graph_->operational_revision();
    }
    const auto found = cache_.find(endpoints);
    if (found != cache_.end()) {
        ++statistics_.hits;
        return found->second;
    }
    ++statistics_.misses;
    auto paths = enumerate(endpoints);
    std::size_t entries{0};
    for (const auto& path : paths) {
        entries += path.size();
    }
    while (cache_.size() >= limits_.maximum_cached_pairs ||
           entries > limits_.maximum_cached_route_entries - statistics_.route_entries) {
        evict_oldest();
    }
    auto inserted = cache_.emplace(endpoints, std::move(paths));
    order_.push_back(endpoints);
    statistics_.route_entries += entries;
    statistics_.paths += inserted.first->second.size();
    return inserted.first->second;
}
CacheStatistics PathService::statistics() const noexcept {
    auto result = statistics_;
    result.pairs = cache_.size();
    return result;
}
} // namespace nexuslab::routing
