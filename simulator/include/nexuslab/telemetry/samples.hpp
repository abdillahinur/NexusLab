// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/time.hpp"
#include "nexuslab/telemetry/metrics.hpp"

#include <cstdint>

namespace nexuslab::telemetry {

struct MetricSample final {
    MetricId metric;
    MetricLabels labels;
    MetricKind kind;
    std::uint64_t value;
    sim::SimTimeNs timestamp;

    bool operator==(const MetricSample&) const = default;
};

[[nodiscard]] bool is_sampled_metric_kind(MetricKind kind) noexcept;

} // namespace nexuslab::telemetry
