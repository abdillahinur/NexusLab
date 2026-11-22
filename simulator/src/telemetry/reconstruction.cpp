// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/telemetry/serialization.hpp"
#include <stdexcept>
namespace nexuslab::telemetry {
namespace {
bool needs_catalog_v2(const TelemetryObservation& observation) {
    if (std::holds_alternative<LoadObservation>(observation)) {
        return true;
    }
    if (const auto* failure = std::get_if<FailureObservation>(&observation)) {
        return failure->details.has_value();
    }
    if (const auto* route = std::get_if<RoutingDecisionObservation>(&observation)) {
        return route->capacity_revision.has_value() || route->request.has_value();
    }
    return false;
}
} // namespace

TelemetrySnapshot reconstruct_full_trace(const TelemetrySnapshot& trace,
                                         const TelemetryRunMetadata& metadata,
                                         std::uint32_t catalog_version) {
    require_supported_catalog_version(catalog_version);
    const auto expected_catalog =
        metadata.script.has_value() ? telemetry_catalog_version_v2 : telemetry_catalog_version;
    if (catalog_version != expected_catalog || trace.configuration.mode != TelemetryMode::Full ||
        !trace.finalized) {
        throw std::invalid_argument{
            "reconstruction requires finalized full records and matching catalog provenance"};
    }
    SummaryBuilder builder{trace.configuration.limits.metric_series,
                           trace.configuration.limits.histogram_boundaries};
    std::uint64_t expected_id{0};
    for (const auto& record : trace.records) {
        if (record.id.value() != expected_id++ || record.timestamp > metadata.final_time) {
            throw std::invalid_argument{"incomplete or out-of-range telemetry record sequence"};
        }
        if (catalog_version == telemetry_catalog_version &&
            (needs_catalog_v2(record.observation) ||
             record.correlation.traffic_source.has_value() ||
             record.correlation.flow.has_value())) {
            throw std::invalid_argument{"catalog-v2 record cannot be replayed as catalog v1"};
        }
        builder.consume(record.timestamp, record.correlation, record.observation);
    }
    builder.finalize(metadata.final_time);
    auto result = trace;
    result.metrics = builder.metric_snapshots();
    result.job_attributions = builder.job_attributions();
    result.perturbations = builder.perturbations();
    if (catalog_version == telemetry_catalog_version && !result.perturbations.empty()) {
        throw std::invalid_argument{
            "catalog-v2 observations cannot be reconstructed as catalog v1"};
    }
    return result;
}
} // namespace nexuslab::telemetry
