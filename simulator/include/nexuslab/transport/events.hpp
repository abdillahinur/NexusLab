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

} // namespace nexuslab::transport
