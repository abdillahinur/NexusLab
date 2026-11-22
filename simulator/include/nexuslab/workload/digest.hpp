// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/workload/run.hpp"

#include <cstdint>

namespace nexuslab::workload {

inline constexpr std::uint32_t domain_outcome_digest_version = 1;

[[nodiscard]] inline std::uint32_t
resolved_domain_outcome_digest_version(const TrainingReport& report) noexcept {
    return report.failures.has_value() ? 2U : domain_outcome_digest_version;
}

// Hashes authoritative domain results while deliberately excluding telemetry and the
// full-mode-only kernel trace hash.
[[nodiscard]] std::uint64_t domain_outcome_digest(const TrainingReport& report) noexcept;

} // namespace nexuslab::workload
