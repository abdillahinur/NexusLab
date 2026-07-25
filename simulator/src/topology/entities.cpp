// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/topology/entities.hpp"

namespace nexuslab::topology {

std::array<DirectedLink, 2> directed_links(const PhysicalLink& link) noexcept {
    return std::array{
        DirectedLink{DirectedLinkId{link.id, LinkDirection::AToB}, link.endpoint_a,
                     link.endpoint_b},
        DirectedLink{DirectedLinkId{link.id, LinkDirection::BToA}, link.endpoint_b,
                     link.endpoint_a},
    };
}

} // namespace nexuslab::topology
