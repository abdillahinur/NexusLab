// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nexuslab/routing/paths.hpp"
#include "nexuslab/transport/runtime.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace nexuslab::routing {
struct RouteRequest final {
    std::uint64_t flow;
    Endpoints endpoints;
    transport::ByteCount bytes;
    transport::ByteCount maximum_chunk_bytes;
    bool operator==(const RouteRequest&) const = default;
};
struct LinkLoad final {
    transport::ByteCount outstanding_bytes;
    transport::BitsPerSecond bandwidth;
    sim::SimDurationNs propagation;
};
class FabricView final {
  public:
    explicit FabricView(const transport::TransportRuntime& runtime) : runtime_{&runtime} {}
    [[nodiscard]] LinkLoad read(topology::DirectedLinkId link) const;

  private:
    const transport::TransportRuntime* runtime_;
};
struct RouteChoice final {
    std::size_t candidate;
    std::uint64_t score;
    std::string reason;
    bool operator==(const RouteChoice&) const = default;
};
struct PolicyInput final {
    const RouteRequest& request;
    std::uint64_t seed;
    std::span<const Path> candidates;
    FabricView fabric;
};
class RoutingPolicy {
  public:
    virtual ~RoutingPolicy() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t version() const noexcept = 0;
    [[nodiscard]] virtual RouteChoice choose(const PolicyInput& input) const = 0;
};
class PolicyRegistry final {
  public:
    using Factory = std::function<std::unique_ptr<RoutingPolicy>()>;
    PolicyRegistry();
    void add(std::string name, Factory factory);
    [[nodiscard]] std::unique_ptr<RoutingPolicy> create(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;

  private:
    std::map<std::string, Factory, std::less<>> factories_;
};
[[nodiscard]] std::uint64_t ecmp_hash(const RouteRequest& request, std::uint64_t seed) noexcept;
} // namespace nexuslab::routing
