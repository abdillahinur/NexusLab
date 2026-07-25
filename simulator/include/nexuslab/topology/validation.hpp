// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/graph.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nexuslab::topology {

enum class ValidationErrorCode : std::uint8_t {
    EmptyTopology = 1,
    NonDenseId = 2,
    UnknownReference = 3,
    RelationshipMismatch = 4,
    PortOwnershipMismatch = 5,
    PortLinkCountMismatch = 6,
    LinkEndpointMismatch = 7,
    LinkKindMismatch = 8,
    AdjacencyMismatch = 9,
    DisconnectedNode = 10,
};

struct ValidationError final {
    ValidationErrorCode code;
    std::optional<NodeId> node;
    std::optional<LinkId> link;
    std::string message;

    bool operator==(const ValidationError&) const = default;
};

struct ValidationReport final {
    std::vector<ValidationError> errors;

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] ValidationReport validate_topology(const TopologyGraph& graph);

} // namespace nexuslab::topology
