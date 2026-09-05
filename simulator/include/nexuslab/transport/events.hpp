// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/transport/types.hpp"

#include <cstdint>

namespace nexuslab::transport {

struct ChunkArrivalEvent final {
    ChunkId chunk;
    std::uint32_t hop_index;

    bool operator==(const ChunkArrivalEvent&) const = default;
};

struct TransmissionCompleteEvent final {
    topology::LinkId link;
    ChunkId chunk;
    std::uint32_t hop_index;
    topology::LinkDirection direction;

    bool operator==(const TransmissionCompleteEvent&) const = default;
};

struct LinkStateChangeEvent final {
    topology::LinkId link;
    topology::OperationalState state;

    bool operator==(const LinkStateChangeEvent&) const = default;
};

struct PortStateChangeEvent final {
    topology::PortId port;
    topology::OperationalState state;
    bool operator==(const PortStateChangeEvent&) const = default;
};

struct SwitchStateChangeEvent final {
    topology::SwitchId network_switch;
    topology::OperationalState state;
    bool operator==(const SwitchStateChangeEvent&) const = default;
};

} // namespace nexuslab::transport
