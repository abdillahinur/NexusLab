// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/id.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

namespace nexuslab::topology {

enum class NodeKind : std::uint8_t {
    Gpu = 1,
    Nic = 2,
    Switch = 3,
};

enum class SwitchRole : std::uint8_t {
    Leaf = 1,
    Spine = 2,
};

enum class PortRole : std::uint8_t {
    Local = 1,
    FabricDownlink = 2,
    FabricUplink = 3,
};

enum class LinkKind : std::uint8_t {
    LocalAttachment = 1,
    Fabric = 2,
};

enum class OperationalState : std::uint8_t {
    Up = 1,
    Down = 2,
};

enum class LinkDirection : std::uint8_t {
    AToB = 1,
    BToA = 2,
};

class NodeId final {
  public:
    explicit constexpr NodeId(GpuId id) noexcept : kind_{NodeKind::Gpu}, value_{id.value()} {}
    explicit constexpr NodeId(NicId id) noexcept : kind_{NodeKind::Nic}, value_{id.value()} {}
    explicit constexpr NodeId(SwitchId id) noexcept : kind_{NodeKind::Switch}, value_{id.value()} {}

    [[nodiscard]] constexpr NodeKind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    auto operator<=>(const NodeId&) const = default;

  private:
    NodeKind kind_;
    std::uint64_t value_;
};

struct GpuWorker final {
    GpuId id;
    RackId rack;
    NicId attached_nic;
    PortId port;

    bool operator==(const GpuWorker&) const = default;
};

struct Nic final {
    NicId id;
    RackId rack;
    std::vector<GpuId> attached_gpus;
    std::vector<PortId> ports;

    bool operator==(const Nic&) const = default;
};

struct Switch final {
    SwitchId id;
    std::optional<RackId> rack;
    SwitchRole role;
    std::vector<PortId> ports;
    OperationalState state{OperationalState::Up};

    bool operator==(const Switch&) const = default;
};

struct Rack final {
    RackId id;
    std::vector<GpuId> gpus;
    std::vector<NicId> nics;
    std::vector<SwitchId> leaf_switches;

    bool operator==(const Rack&) const = default;
};

struct Port final {
    PortId id;
    NodeId owner;
    PortRole role;
    OperationalState state{OperationalState::Up};

    bool operator==(const Port&) const = default;
};

struct PhysicalLink final {
    LinkId id;
    PortId endpoint_a;
    PortId endpoint_b;
    LinkKind kind;
    OperationalState state{OperationalState::Up};

    bool operator==(const PhysicalLink&) const = default;
};

struct DirectedLinkId final {
    LinkId link;
    LinkDirection direction;

    auto operator<=>(const DirectedLinkId&) const = default;
};

struct DirectedLink final {
    DirectedLinkId id;
    PortId source;
    PortId destination;

    bool operator==(const DirectedLink&) const = default;
};

[[nodiscard]] std::array<DirectedLink, 2> directed_links(const PhysicalLink& link) noexcept;

} // namespace nexuslab::topology
