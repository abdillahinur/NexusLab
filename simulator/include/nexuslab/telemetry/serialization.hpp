// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/telemetry/session.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nexuslab::telemetry {

inline constexpr std::uint32_t telemetry_schema_version = 1;
inline constexpr std::uint32_t telemetry_catalog_version = 1;

inline constexpr std::uint32_t telemetry_catalog_version_v2 = 2;
void require_supported_catalog_version(std::uint32_t version);
void require_supported_schema_version(std::uint32_t version);
struct ScriptMetadata final {
    std::uint64_t script_digest;
    std::uint64_t outcome_digest;
    std::uint32_t outcome_version;
    std::uint64_t capacity_revision;
    bool domain_success;
    bool operator==(const ScriptMetadata&) const = default;
};

enum class TelemetryRunOutcome : std::uint8_t {
    Completed = 1,
    Stopped = 2,
    Failed = 3,
};

struct TelemetryRunMetadata final {
    std::uint64_t seed{0};
    std::uint64_t scenario_digest{0};
    std::string routing_policy;
    std::uint64_t routing_policy_version{1};
    std::optional<std::string> scheduling_policy;
    std::uint64_t scheduling_policy_version{1};
    TelemetryRunOutcome outcome{TelemetryRunOutcome::Completed};
    sim::SimTimeNs final_time;
    bool synthetic{true};
    bool complete{true};
    std::optional<ScriptMetadata> script{std::nullopt};

    bool operator==(const TelemetryRunMetadata&) const = default;
};

// Replays a finalized in-memory full trace; JSONL parsing/import is a separate boundary.
[[nodiscard]] TelemetrySnapshot reconstruct_full_trace(const TelemetrySnapshot& trace,
                                                       const TelemetryRunMetadata& metadata,
                                                       std::uint32_t catalog_version);
[[nodiscard]] std::string_view run_outcome_name(TelemetryRunOutcome outcome);
[[nodiscard]] std::uint64_t fnv1a64(std::string_view content) noexcept;
[[nodiscard]] std::string serialize_summary_json(const TelemetrySnapshot& snapshot,
                                                 const TelemetryRunMetadata& metadata);
[[nodiscard]] std::string serialize_records_jsonl(const TelemetrySnapshot& snapshot,
                                                  const TelemetryRunMetadata& metadata);

} // namespace nexuslab::telemetry
