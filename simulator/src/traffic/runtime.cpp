// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/traffic/runtime.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>
namespace nexuslab::traffic {
std::string_view kind_name(Kind kind) {
    switch (kind) {
    case Kind::Burst:
        return "burst";
    case Kind::Checkpoint:
        return "checkpoint";
    }
    throw std::invalid_argument{"unknown background load kind"};
}
std::string_view outcome_name(Outcome outcome) {
    switch (outcome) {
    case Outcome::Pending:
        return "pending";
    case Outcome::Succeeded:
        return "succeeded";
    case Outcome::Failed:
        return "failed";
    case Outcome::NoRoute:
        return "no_route";
    }
    throw std::invalid_argument{"unknown background load outcome"};
}
namespace {
void validate_source(const Source& source, const topology::TopologyGraph& graph) {
    static_cast<void>(kind_name(source.kind));
    const auto* from = graph.find(source.source);
    const auto* to = graph.find(source.destination);
    if (from == nullptr || to == nullptr || from->attached_nic == to->attached_nic) {
        throw std::invalid_argument{"background load requires GPUs on different known NICs"};
    }
    if (source.count == 0 || source.bytes.value() == 0 || source.chunk_bytes.value() == 0) {
        throw std::invalid_argument{"background load counts and sizes must be positive"};
    }
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (source.bytes.value() > maximum / source.count ||
        (source.interval.count() != 0 &&
         source.count - 1 > (maximum - source.onset.count()) / source.interval.count())) {
        throw std::overflow_error{"background load arithmetic overflow"};
    }
}
} // namespace
std::vector<Request> validate(const std::vector<Source>& sources,
                              const topology::TopologyGraph& graph, Limits limits) {
    if (limits.sources == 0 || limits.requests == 0 || sources.size() > limits.sources) {
        throw std::length_error{"background source limit exceeded"};
    }
    std::size_t count{0};
    std::uint64_t total_bytes{0};
    for (const auto& source : sources) {
        validate_source(source, graph);
        if (source.count > limits.requests - count) {
            throw std::length_error{"background request limit exceeded"};
        }
        const auto bytes = source.count * source.bytes.value();
        if (bytes > std::numeric_limits<std::uint64_t>::max() - total_bytes) {
            throw std::overflow_error{"background total byte count overflow"};
        }
        total_bytes += bytes;
        count += static_cast<std::size_t>(source.count);
    }
    std::vector<Request> requests;
    requests.reserve(count);
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        for (std::uint64_t k = 0; k < source.count; ++k) {
            requests.push_back(
                {TrafficSourceId{i + 1}, k,
                 sim::SimTimeNs{source.onset.count() + k * source.interval.count()}});
        }
    }
    return requests;
}
Runtime::Runtime(std::vector<Source> sources, const topology::TopologyGraph& graph,
                 routing::Router& router, Limits limits)
    : sources_{std::move(sources)}, graph_{&graph}, router_{&router},
      requests_{validate(sources_, graph, limits)} {
    for (const auto& source : sources_) {
        router_->preflight(
            {0,
             {topology::NodeId{graph.find(source.source)->attached_nic},
              topology::NodeId{graph.find(source.destination)->attached_nic}},
             source.bytes,
             source.chunk_bytes},
            sim::SimTimeNs{source.onset.count() + (source.count - 1) * source.interval.count()});
    }
}
void Runtime::schedule(sim::Simulation& simulation) {
    if (scheduled_) {
        throw std::logic_error{"background loads already scheduled"};
    }
    scheduled_ = true;
    std::vector<std::size_t> order(requests_.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& left = requests_[a];
        const auto& right = requests_[b];
        return std::tuple{left.scheduled, left.source, left.ordinal} <
               std::tuple{right.scheduled, right.source, right.ordinal};
    });
    for (const auto index : order) {
        const auto id = simulation.schedule(
            {requests_[index].scheduled, sim::EventPriority::Normal, InjectionEvent{index}});
        injections_.emplace(id, index);
    }
}
void Runtime::handle(const InjectionEvent& event, sim::SimulationContext& context) {
    const auto found = injections_.find(context.current_event_id());
    if (found == injections_.end() || found->second != event.request) {
        throw std::logic_error{"unexpected background injection event"};
    }
    auto& request = requests_.at(found->second);
    const auto& source = sources_.at(static_cast<std::size_t>(request.source.value() - 1));
    request.injected = context.now();
    // The upper half of the flow-key space is reserved for background requests.
    const auto submitted =
        router_->submit({(std::uint64_t{1} << 63U) | event.request,
                         {topology::NodeId{graph_->find(source.source)->attached_nic},
                          topology::NodeId{graph_->find(source.destination)->attached_nic}},
                         source.bytes,
                         source.chunk_bytes},
                        context);
    injections_.erase(found);
    if (!submitted.has_value()) {
        request.outcome = Outcome::NoRoute;
        request.finished = context.now();
        return;
    }
    request.transfer = submitted->id;
    pending_.emplace(submitted->id, static_cast<std::size_t>(event.request));
}
void Runtime::handle(const transport::TransferCompletion& completion) {
    const auto found = pending_.find(completion.transfer);
    if (found == pending_.end()) {
        throw std::logic_error{"unowned background completion"};
    }
    auto& request = requests_.at(found->second);
    request.outcome = completion.outcome == transport::TransferOutcome::Succeeded
                          ? Outcome::Succeeded
                          : Outcome::Failed;
    request.finished = completion.timestamp;
    request.delivered = completion.delivered;
    request.dropped_buffer_full = completion.dropped_buffer_full;
    request.dropped_link_down = completion.dropped_link_down;
    pending_.erase(found);
}
bool Runtime::owns(transport::TransferId transfer) const { return pending_.contains(transfer); }
const std::vector<Request>& Runtime::report() const noexcept { return requests_; }
} // namespace nexuslab::traffic
