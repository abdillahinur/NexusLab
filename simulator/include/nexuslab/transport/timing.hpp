// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/time.hpp"
#include "nexuslab/transport/types.hpp"

#include <optional>

namespace nexuslab::transport {

[[nodiscard]] std::optional<sim::SimDurationNs>
serialization_delay(ByteCount bytes, BitsPerSecond bandwidth) noexcept;

} // namespace nexuslab::transport
