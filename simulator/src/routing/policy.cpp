// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/routing/policy.hpp"
#include "nexuslab/transport/timing.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nexuslab::routing {
namespace {
[[nodiscard]] std::uint64_t add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error{"routing score overflow"};
    }
    return left + right;
}
enum class Kind : std::uint8_t { Shortest, Ecmp, LeastLoaded, QueueAware };
[[nodiscard]] std::uint64_t score(const Path& path, const PolicyInput& input, Kind kind) {
    std::uint64_t result{0};
    for (const auto link : path) {
        const auto load = input.fabric.read(link);
        if (kind == Kind::LeastLoaded) {
            result = add(result, load.outstanding_bytes.value());
        } else {
            const auto chunk = std::min(input.request.bytes, input.request.maximum_chunk_bytes);
            const auto bytes = add(load.outstanding_bytes.value(), chunk.value());
            const auto delay =
                transport::serialization_delay(transport::ByteCount{bytes}, load.bandwidth);
            if (!delay.has_value()) {
                throw std::overflow_error{"queue-aware delay overflow"};
            }
            result = add(result, add(delay->count(), load.propagation.count()));
        }
    }
    return result;
}
class BaselinePolicy final : public RoutingPolicy {
  public:
    BaselinePolicy(std::string name, Kind kind) : name_{std::move(name)}, kind_{kind} {}
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::uint64_t version() const noexcept override { return 1; }
    [[nodiscard]] RouteChoice choose(const PolicyInput& input) const override {
        if (input.candidates.empty() || input.request.bytes.value() == 0 ||
            input.request.maximum_chunk_bytes.value() == 0) {
            throw std::invalid_argument{
                "policy requires candidates and nonzero transfer quantities"};
        }
        if (kind_ == Kind::Shortest) {
            return {0, 0, "canonical minimum-hop path"};
        }
        if (kind_ == Kind::Ecmp) {
            return {static_cast<std::size_t>(ecmp_hash(input.request, input.seed) %
                                             input.candidates.size()),
                    0, "stable flow hash over all equal-hop paths"};
        }
        RouteChoice best{0, score(input.candidates.front(), input, kind_),
                         kind_ == Kind::LeastLoaded ? "minimum outstanding byte sum"
                                                    : "minimum estimated first-chunk delay ns"};
        for (std::size_t index = 1; index < input.candidates.size(); ++index) {
            const auto candidate_score = score(input.candidates[index], input, kind_);
            if (candidate_score < best.score) {
                best.candidate = index;
                best.score = candidate_score;
            }
        }
        return best;
    }

  private:
    std::string name_;
    Kind kind_;
};
} // namespace
LinkLoad FabricView::read(topology::DirectedLinkId link) const {
    const auto* service = runtime_->find_service(link);
    if (service == nullptr) {
        throw std::invalid_argument{"routing path uses unconfigured transport arc"};
    }
    const auto& queue = service->queue();
    const auto* active = queue.active();
    return {transport::ByteCount{add(queue.snapshot().waiting_bytes.value(),
                                     active == nullptr ? 0 : active->bytes.value())},
            queue.configuration().bandwidth, queue.configuration().propagation_delay};
}
std::uint64_t ecmp_hash(const RouteRequest& request, std::uint64_t seed) noexcept {
    std::uint64_t hash{14695981039346656037ULL};
    for (const auto field : {std::uint64_t{1}, seed, request.flow,
                             static_cast<std::uint64_t>(request.endpoints.source.kind()),
                             request.endpoints.source.value(),
                             static_cast<std::uint64_t>(request.endpoints.destination.kind()),
                             request.endpoints.destination.value()}) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            hash ^= (field >> shift) & 255U;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}
PolicyRegistry::PolicyRegistry() {
    add("shortest-path",
        [] { return std::make_unique<BaselinePolicy>("shortest-path", Kind::Shortest); });
    add("ecmp", [] { return std::make_unique<BaselinePolicy>("ecmp", Kind::Ecmp); });
    add("least-loaded",
        [] { return std::make_unique<BaselinePolicy>("least-loaded", Kind::LeastLoaded); });
    add("queue-aware",
        [] { return std::make_unique<BaselinePolicy>("queue-aware", Kind::QueueAware); });
}
void PolicyRegistry::add(std::string name, Factory factory) {
    if (name.empty() || !factory || factories_.contains(name)) {
        throw std::invalid_argument{"invalid or duplicate routing policy registration"};
    }
    factories_.emplace(std::move(name), std::move(factory));
}
std::unique_ptr<RoutingPolicy> PolicyRegistry::create(std::string_view name) const {
    const auto found = factories_.find(name);
    if (found == factories_.end()) {
        throw std::invalid_argument{"unknown routing policy"};
    }
    auto policy = found->second();
    if (!policy || policy->name() != name || policy->version() == 0) {
        throw std::invalid_argument{"routing factory returned invalid policy identity"};
    }
    return policy;
}
std::vector<std::string> PolicyRegistry::names() const {
    std::vector<std::string> result;
    for (const auto& [name, factory] : factories_) {
        static_cast<void>(factory);
        result.push_back(name);
    }
    return result;
}
} // namespace nexuslab::routing
