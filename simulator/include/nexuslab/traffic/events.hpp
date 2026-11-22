// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
namespace nexuslab::traffic {
struct InjectionEvent final {
    std::uint64_t request;
    bool operator==(const InjectionEvent&) const = default;
};
} // namespace nexuslab::traffic
