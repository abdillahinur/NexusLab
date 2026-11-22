// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/sim/time.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/transport/runtime.hpp"
namespace nexuslab::traffic {
using TrafficSourceId = transport::StrongUnsigned<struct TrafficSourceIdTag>;
enum class Kind : std::uint8_t { Burst = 1, Checkpoint = 2 };
struct Source final {
    Kind kind;
    topology::GpuId source;
    topology::GpuId destination;
    sim::SimTimeNs onset;
    std::uint64_t count;
    sim::SimDurationNs interval;
    transport::ByteCount bytes;
    transport::ByteCount chunk_bytes;
    bool operator==(const Source&) const = default;
};
struct Limits final {
    std::size_t sources{10'000};
    std::size_t requests{100'000};
};
enum class Outcome : std::uint8_t { Pending = 1, Succeeded = 2, Failed = 3, NoRoute = 4 };
struct Request final {
    TrafficSourceId source;
    std::uint64_t ordinal;
    sim::SimTimeNs scheduled;
    std::optional<sim::SimTimeNs> injected{std::nullopt};
    std::optional<transport::TransferId> transfer{std::nullopt};
    Outcome outcome{Outcome::Pending};
    std::optional<sim::SimTimeNs> finished{std::nullopt};
    transport::TrafficCount delivered{};
    transport::TrafficCount dropped_buffer_full{};
    transport::TrafficCount dropped_link_down{};
    bool operator==(const Request&) const = default;
};
[[nodiscard]] std::string_view kind_name(Kind kind);
[[nodiscard]] std::string_view outcome_name(Outcome outcome);
// Complete validation and bounded expansion before any event registration.
[[nodiscard]] std::vector<Request> validate(const std::vector<Source>& sources,
                                            const topology::TopologyGraph& graph,
                                            Limits limits = {});
} // namespace nexuslab::traffic
