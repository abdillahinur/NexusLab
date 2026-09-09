// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/samples.hpp"

namespace nexuslab::telemetry {

bool is_sampled_metric_kind(MetricKind kind) noexcept {
    return kind == MetricKind::Counter || kind == MetricKind::Gauge;
}

} // namespace nexuslab::telemetry
