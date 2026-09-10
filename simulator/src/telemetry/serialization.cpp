// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/serialization.hpp"

#include "nexuslab/version.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <type_traits>

namespace nexuslab::telemetry {
namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

void append_u64(std::string& output, std::uint64_t value) {
    std::array<char, 20> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        throw std::logic_error{"failed to encode telemetry integer"};
    }
    output.append(buffer.data(), result.ptr);
}

void append_bool(std::string& output, bool value) { output += value ? "true" : "false"; }

void append_string(std::string& output, std::string_view value) {
    constexpr std::array hex{'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    output.push_back('"');
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (byte < 0x20U) {
                output += "\\u00";
                output.push_back(hex[byte >> 4U]);
                output.push_back(hex[byte & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    output.push_back('"');
}

[[nodiscard]] std::string hex_u64(std::uint64_t value) {
    std::array<char, 17> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + 16, value, 16);
    if (result.ec != std::errc{}) {
        throw std::logic_error{"failed to encode telemetry digest"};
    }
    const auto digits = static_cast<std::size_t>(result.ptr - buffer.data());
    std::string output(16 - digits, '0');
    output.append(buffer.data(), result.ptr);
    return output;
}

void hash_text(std::uint64_t& hash, std::string_view text) noexcept {
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        hash ^= byte;
        hash *= fnv_prime;
    }
}

[[nodiscard]] std::string_view label_name(MetricLabel label) {
    switch (label) {
    case MetricLabel::Job:
        return "job";
    case MetricLabel::Collective:
        return "collective";
    case MetricLabel::Link:
        return "link";
    case MetricLabel::Direction:
        return "direction";
    case MetricLabel::Policy:
        return "policy";
    case MetricLabel::Outcome:
        return "outcome";
    case MetricLabel::Reason:
        return "reason";
    case MetricLabel::Failure:
        return "failure";
    case MetricLabel::Resource:
        return "resource";
    }
    throw std::invalid_argument{"unknown telemetry metric label"};
}

[[nodiscard]] std::string_view record_kind_name(TelemetryRecordKind kind) {
    switch (kind) {
    case TelemetryRecordKind::Counter:
        return "counter";
    case TelemetryRecordKind::Gauge:
        return "gauge";
    case TelemetryRecordKind::Histogram:
        return "histogram";
    case TelemetryRecordKind::Simulation:
        return "simulation";
    case TelemetryRecordKind::Job:
        return "job";
    case TelemetryRecordKind::Collective:
        return "collective";
    case TelemetryRecordKind::Transfer:
        return "transfer";
    case TelemetryRecordKind::Queue:
        return "queue";
    case TelemetryRecordKind::Failure:
        return "failure";
    case TelemetryRecordKind::RoutingDecision:
        return "routing_decision";
    case TelemetryRecordKind::PlacementDecision:
        return "placement_decision";
    }
    throw std::invalid_argument{"unknown telemetry record kind"};
}

[[nodiscard]] std::string_view simulation_transition_name(SimulationTransition transition) {
    switch (transition) {
    case SimulationTransition::RunStarted:
        return "run_started";
    case SimulationTransition::EventScheduled:
        return "event_scheduled";
    case SimulationTransition::EventDispatched:
        return "event_dispatched";
    case SimulationTransition::EventCancelled:
        return "event_cancelled";
    case SimulationTransition::StopRequested:
        return "stop_requested";
    case SimulationTransition::RunSucceeded:
        return "run_succeeded";
    case SimulationTransition::RunFailed:
        return "run_failed";
    case SimulationTransition::RunStopped:
        return "run_stopped";
    }
    throw std::invalid_argument{"unknown simulation telemetry transition"};
}

[[nodiscard]] std::string_view job_transition_name(JobTransition transition) {
    switch (transition) {
    case JobTransition::Arrived:
        return "arrived";
    case JobTransition::Waiting:
        return "waiting";
    case JobTransition::Admitted:
        return "admitted";
    case JobTransition::ComputeStarted:
        return "compute_started";
    case JobTransition::ComputeCompleted:
        return "compute_completed";
    case JobTransition::CollectiveStarted:
        return "collective_started";
    case JobTransition::CollectiveCompleted:
        return "collective_completed";
    case JobTransition::StepCompleted:
        return "step_completed";
    case JobTransition::Succeeded:
        return "succeeded";
    case JobTransition::Failed:
        return "failed";
    case JobTransition::Cancelled:
        return "cancelled";
    case JobTransition::SynchronizationStarted:
        return "synchronization_started";
    case JobTransition::StragglerStarted:
        return "straggler_started";
    }
    throw std::invalid_argument{"unknown job telemetry transition"};
}

[[nodiscard]] std::string_view collective_transition_name(CollectiveTransition transition) {
    switch (transition) {
    case CollectiveTransition::Submitted:
        return "submitted";
    case CollectiveTransition::PhaseStarted:
        return "phase_started";
    case CollectiveTransition::RoundStarted:
        return "round_started";
    case CollectiveTransition::RoundCompleted:
        return "round_completed";
    case CollectiveTransition::TransferIssued:
        return "transfer_issued";
    case CollectiveTransition::Succeeded:
        return "succeeded";
    case CollectiveTransition::Failed:
        return "failed";
    case CollectiveTransition::Cancelled:
        return "cancelled";
    }
    throw std::invalid_argument{"unknown collective telemetry transition"};
}

[[nodiscard]] std::string_view transfer_transition_name(TransferTransition transition) {
    switch (transition) {
    case TransferTransition::Submitted:
        return "submitted";
    case TransferTransition::ChunkQueued:
        return "chunk_queued";
    case TransferTransition::ChunkServiceStarted:
        return "chunk_service_started";
    case TransferTransition::ChunkSerialized:
        return "chunk_serialized";
    case TransferTransition::ChunkDelivered:
        return "chunk_delivered";
    case TransferTransition::ChunkDropped:
        return "chunk_dropped";
    case TransferTransition::Succeeded:
        return "succeeded";
    case TransferTransition::Failed:
        return "failed";
    }
    throw std::invalid_argument{"unknown transfer telemetry transition"};
}

[[nodiscard]] std::string_view transfer_reason_name(TransferReason reason) {
    switch (reason) {
    case TransferReason::None:
        return "none";
    case TransferReason::BufferFull:
        return "buffer_full";
    case TransferReason::ResourceDown:
        return "resource_down";
    }
    throw std::invalid_argument{"unknown transfer telemetry reason"};
}

[[nodiscard]] std::string_view queue_transition_name(QueueTransition transition) {
    switch (transition) {
    case QueueTransition::Enqueued:
        return "enqueued";
    case QueueTransition::ServiceStarted:
        return "service_started";
    case QueueTransition::ServiceCompleted:
        return "service_completed";
    case QueueTransition::Marked:
        return "marked";
    case QueueTransition::Dropped:
        return "dropped";
    }
    throw std::invalid_argument{"unknown queue telemetry transition"};
}

[[nodiscard]] std::string_view routing_outcome_name(RoutingDecisionOutcome outcome) {
    switch (outcome) {
    case RoutingDecisionOutcome::Selected:
        return "selected";
    case RoutingDecisionOutcome::NoRoute:
        return "no_route";
    case RoutingDecisionOutcome::Rejected:
        return "rejected";
    }
    throw std::invalid_argument{"unknown routing telemetry outcome"};
}

[[nodiscard]] std::string_view placement_outcome_name(PlacementDecisionOutcome outcome) {
    switch (outcome) {
    case PlacementDecisionOutcome::Placed:
        return "placed";
    case PlacementDecisionOutcome::Waiting:
        return "waiting";
    case PlacementDecisionOutcome::Rejected:
        return "rejected";
    }
    throw std::invalid_argument{"unknown placement telemetry outcome"};
}

[[nodiscard]] std::string_view failure_transition_name(FailureTransition transition) {
    switch (transition) {
    case FailureTransition::Scheduled:
        return "scheduled";
    case FailureTransition::Detected:
        return "detected";
    case FailureTransition::Applied:
        return "applied";
    case FailureTransition::RecoveryStarted:
        return "recovery_started";
    case FailureTransition::Recovered:
        return "recovered";
    }
    throw std::invalid_argument{"unknown failure telemetry transition"};
}

[[nodiscard]] std::string_view failure_resource_name(FailureResourceKind resource) {
    switch (resource) {
    case FailureResourceKind::Link:
        return "link";
    case FailureResourceKind::Port:
        return "port";
    case FailureResourceKind::Switch:
        return "switch";
    case FailureResourceKind::Gpu:
        return "gpu";
    case FailureResourceKind::Nic:
        return "nic";
    }
    throw std::invalid_argument{"unknown failure telemetry resource"};
}

void append_labels(std::string& output, const MetricLabels& labels) {
    output.push_back('[');
    bool first = true;
    for (const MetricLabelValue label : labels.values()) {
        if (!first) {
            output.push_back(',');
        }
        first = false;
        output += "{\"name\":";
        append_string(output, label_name(label.label));
        output += ",\"value\":";
        append_u64(output, label.value);
        output.push_back('}');
    }
    output.push_back(']');
}

void append_label_mask(std::string& output, MetricLabelMask mask) {
    output.push_back('[');
    bool first = true;
    for (auto raw = static_cast<std::uint8_t>(MetricLabel::Job);
         raw <= static_cast<std::uint8_t>(MetricLabel::Resource); ++raw) {
        const auto label = static_cast<MetricLabel>(raw);
        if ((mask & label_mask(label)) != 0) {
            if (!first) {
                output.push_back(',');
            }
            first = false;
            append_string(output, label_name(label));
        }
    }
    output.push_back(']');
}

void append_u64_array(std::string& output, std::span<const std::uint64_t> values) {
    output.push_back('[');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_u64(output, values[index]);
    }
    output.push_back(']');
}

template <typename Id> void append_id_array(std::string& output, std::span<const Id> values) {
    output.push_back('[');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_u64(output, values[index].value());
    }
    output.push_back(']');
}

void append_id_array(std::string& output, std::span<const topology::DirectedLinkId> values) {
    output.push_back('[');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        output += "{\"link\":";
        append_u64(output, values[index].link.value());
        output += ",\"direction\":";
        append_u64(output, static_cast<std::uint8_t>(values[index].direction));
        output.push_back('}');
    }
    output.push_back(']');
}

void append_limits(std::string& output, const TelemetryLimits& limits) {
    output += "{\"metric_series\":";
    append_u64(output, limits.metric_series);
    output += ",\"decision_records\":";
    append_u64(output, limits.decision_records);
    output += ",\"domain_records\":";
    append_u64(output, limits.domain_records);
    output += ",\"samples\":";
    append_u64(output, limits.samples);
    output += ",\"histogram_boundaries\":";
    append_u64(output, limits.histogram_boundaries);
    output += ",\"correlation_edges\":";
    append_u64(output, limits.correlation_edges);
    output += ",\"serialized_bytes\":";
    append_u64(output, limits.serialized_bytes);
    output.push_back('}');
}

void append_configuration(std::string& output, const TelemetryConfiguration& configuration) {
    output += "{\"mode\":";
    append_string(output, mode_name(configuration.mode));
    output += ",\"sample_interval_ns\":";
    append_u64(output, configuration.sample_interval_ns);
    output += ",\"catalog_version\":";
    append_u64(output, telemetry_catalog_version);
    output += ",\"limits\":";
    append_limits(output, configuration.limits);
    output.push_back('}');
}

void append_common_metadata(std::string& output, std::string_view schema,
                            const TelemetrySnapshot& snapshot,
                            const TelemetryRunMetadata& metadata) {
    output += "\"schema\":";
    append_string(output, schema);
    output += ",\"schema_version\":";
    append_u64(output, telemetry_schema_version);
    output += ",\"simulator_version\":";
    append_string(output, nexuslab::version());
    output += ",\"synthetic\":";
    append_bool(output, metadata.synthetic);
    output += ",\"seed\":";
    append_u64(output, metadata.seed);
    output += ",\"scenario_digest_fnv1a64\":";
    append_string(output, hex_u64(metadata.scenario_digest));
    output += R"(,"routing_policy":{"name":)";
    append_string(output, metadata.routing_policy);
    output += ",\"version\":";
    append_u64(output, metadata.routing_policy_version);
    output += "},\"scheduling_policy\":";
    if (metadata.scheduling_policy.has_value()) {
        output += "{\"name\":";
        append_string(output, *metadata.scheduling_policy);
        output += ",\"version\":";
        append_u64(output, metadata.scheduling_policy_version);
        output.push_back('}');
    } else {
        output += "null";
    }
    output += ",\"telemetry\":";
    append_configuration(output, snapshot.configuration);
    output += R"(,"terminal":{"status":)";
    append_string(output, run_outcome_name(metadata.outcome));
    output += ",\"complete\":";
    append_bool(output, metadata.complete);
    output += ",\"final_time_ns\":";
    append_u64(output, metadata.final_time.count());
    output.push_back('}');
}

void append_metric_definition(std::string& output, const MetricDefinition& definition) {
    output += "{\"id\":";
    append_u64(output, static_cast<std::uint8_t>(definition.id));
    output += ",\"name\":";
    append_string(output, definition.name);
    output += ",\"description\":";
    append_string(output, definition.description);
    output += ",\"kind\":";
    append_string(output, kind_name(definition.kind));
    output += ",\"unit\":";
    append_string(output, unit_name(definition.unit));
    output += ",\"required_labels\":";
    append_label_mask(output, definition.required_labels);
    output += ",\"allowed_labels\":";
    append_label_mask(output, definition.allowed_labels);
    output += ",\"histogram_boundaries\":";
    append_u64_array(output, definition.histogram_boundaries);
    output.push_back('}');
}

void append_metric(std::string& output, const MetricSeriesSnapshot& metric) {
    output += "{\"id\":";
    append_u64(output, static_cast<std::uint8_t>(metric.metric));
    output += ",\"labels\":";
    append_labels(output, metric.labels);
    output += ",\"kind\":";
    append_string(output, kind_name(metric.kind));
    if (metric.kind == MetricKind::Histogram) {
        output += ",\"buckets\":";
        append_u64_array(output, metric.histogram_buckets);
        output += ",\"count\":";
        append_u64(output, metric.histogram_count);
        output += ",\"sum\":";
        append_u64(output, metric.histogram_sum);
    } else {
        output += ",\"value\":";
        append_u64(output, metric.scalar);
    }
    output.push_back('}');
}

void append_attribution(std::string& output, const JobAttributionSnapshot& attribution) {
    output += "{\"job\":";
    append_u64(output, attribution.job.value());
    output += ",\"scheduling_wait_ns\":";
    append_u64(output, attribution.scheduling_wait_ns);
    output += ",\"compute_ns\":";
    append_u64(output, attribution.compute_ns);
    output += ",\"communication_ns\":";
    append_u64(output, attribution.communication_ns);
    output += ",\"synchronization_wait_ns\":";
    append_u64(output, attribution.synchronization_wait_ns);
    output += ",\"straggler_delay_ns\":";
    append_u64(output, attribution.straggler_delay_ns);
    output += ",\"terminal_other_ns\":";
    append_u64(output, attribution.terminal_other_ns);
    output += ",\"accounted_ns\":";
    append_u64(output, attribution.accounted_ns());
    output += ",\"terminal\":";
    append_bool(output, attribution.terminal);
    output.push_back('}');
}

template <typename Id>
void append_correlation_field(std::string& output, bool& first, std::string_view name,
                              const std::optional<Id>& value) {
    if (!value.has_value()) {
        return;
    }
    if (!first) {
        output.push_back(',');
    }
    first = false;
    append_string(output, name);
    output.push_back(':');
    append_u64(output, value->value());
}

void append_correlation_field(std::string& output, bool& first, std::string_view name,
                              const std::optional<topology::DirectedLinkId>& value) {
    if (!value.has_value()) {
        return;
    }
    if (!first) {
        output.push_back(',');
    }
    first = false;
    append_string(output, name);
    output += ":{\"link\":";
    append_u64(output, value->link.value());
    output += ",\"direction\":";
    append_u64(output, static_cast<std::uint8_t>(value->direction));
    output.push_back('}');
}

void append_correlation(std::string& output, const Correlation& correlation) {
    output.push_back('{');
    bool first = true;
    append_correlation_field(output, first, "gpu", correlation.gpu);
    append_correlation_field(output, first, "nic", correlation.nic);
    append_correlation_field(output, first, "switch", correlation.switch_entity);
    append_correlation_field(output, first, "port", correlation.port);
    append_correlation_field(output, first, "rack", correlation.rack);
    append_correlation_field(output, first, "link", correlation.link);
    append_correlation_field(output, first, "event", correlation.event);
    append_correlation_field(output, first, "cause", correlation.cause);
    append_correlation_field(output, first, "job", correlation.job);
    append_correlation_field(output, first, "collective", correlation.collective);
    append_correlation_field(output, first, "transfer", correlation.transfer);
    append_correlation_field(output, first, "chunk", correlation.chunk);
    append_correlation_field(output, first, "routing_decision", correlation.routing_decision);
    append_correlation_field(output, first, "placement_decision", correlation.placement_decision);
    append_correlation_field(output, first, "failure", correlation.failure);
    output.push_back('}');
}

void append_observation(std::string& output, const TelemetryObservation& observation) {
    std::visit(
        [&output](const auto& value) {
            using Observation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Observation, CounterObservation>) {
                output += "{\"metric_id\":";
                append_u64(output, static_cast<std::uint8_t>(value.metric));
                output += ",\"labels\":";
                append_labels(output, value.labels);
                output += ",\"amount\":";
                append_u64(output, value.amount);
            } else if constexpr (std::is_same_v<Observation, GaugeObservation>) {
                output += "{\"metric_id\":";
                append_u64(output, static_cast<std::uint8_t>(value.metric));
                output += ",\"labels\":";
                append_labels(output, value.labels);
                output += ",\"value\":";
                append_u64(output, value.value);
            } else if constexpr (std::is_same_v<Observation, HistogramObservation>) {
                output += "{\"metric_id\":";
                append_u64(output, static_cast<std::uint8_t>(value.metric));
                output += ",\"labels\":";
                append_labels(output, value.labels);
                output += ",\"value\":";
                append_u64(output, value.value);
            } else if constexpr (std::is_same_v<Observation, SimulationObservation>) {
                output += "{\"transition\":";
                append_string(output, simulation_transition_name(value.transition));
                output += ",\"pending_events\":";
                append_u64(output, value.pending_events);
            } else if constexpr (std::is_same_v<Observation, JobObservation>) {
                output += "{\"transition\":";
                append_string(output, job_transition_name(value.transition));
                output += ",\"step\":";
                append_u64(output, value.step);
                output += ",\"bucket\":";
                append_u64(output, value.bucket);
                output += ",\"worker\":";
                append_u64(output, value.worker);
                output += ",\"allocated_workers\":";
                append_u64(output, value.allocated_workers);
            } else if constexpr (std::is_same_v<Observation, CollectiveObservation>) {
                output += "{\"transition\":";
                append_string(output, collective_transition_name(value.transition));
                output += ",\"phase\":";
                append_string(output, collective::phase_name(value.phase));
                output += ",\"round\":";
                append_u64(output, value.round);
                output += ",\"bytes\":";
                append_u64(output, value.bytes);
            } else if constexpr (std::is_same_v<Observation, TransferObservation>) {
                output += "{\"transition\":";
                append_string(output, transfer_transition_name(value.transition));
                output += ",\"bytes\":";
                append_u64(output, value.bytes);
                output += ",\"hop\":";
                append_u64(output, value.hop);
                output += ",\"reason\":";
                append_string(output, transfer_reason_name(value.reason));
            } else if constexpr (std::is_same_v<Observation, QueueObservation>) {
                output += "{\"transition\":";
                append_string(output, queue_transition_name(value.transition));
                output += ",\"chunk_bytes\":";
                append_u64(output, value.chunk_bytes);
                output += ",\"waiting_bytes\":";
                append_u64(output, value.waiting_bytes);
                output += ",\"waiting_chunks\":";
                append_u64(output, value.waiting_chunks);
            } else if constexpr (std::is_same_v<Observation, RoutingDecisionObservation>) {
                output += "{\"decision\":";
                append_u64(output, value.decision.value());
                output += ",\"policy\":";
                append_string(output, value.policy);
                output += ",\"policy_version\":";
                append_u64(output, value.policy_version);
                output += ",\"operational_revision\":";
                append_u64(output, value.operational_revision);
                output += ",\"candidate_count\":";
                append_u64(output, value.candidate_count);
                output += ",\"selected_path\":";
                append_id_array(output, std::span{value.selected_path});
                output += ",\"score\":";
                append_u64(output, value.score);
                output += ",\"reason\":";
                append_string(output, value.reason);
                output += ",\"transfer\":";
                if (value.transfer.has_value()) {
                    append_u64(output, value.transfer->value());
                } else {
                    output += "null";
                }
                output += ",\"outcome\":";
                append_string(output, routing_outcome_name(value.outcome));
            } else if constexpr (std::is_same_v<Observation, PlacementDecisionObservation>) {
                output += "{\"decision\":";
                append_u64(output, value.decision.value());
                output += ",\"policy\":";
                append_string(output, value.policy);
                output += ",\"policy_version\":";
                append_u64(output, value.policy_version);
                output += ",\"priority\":";
                append_u64(output, value.priority);
                output += ",\"requested_workers\":";
                append_u64(output, value.requested_workers);
                output += ",\"selected_workers\":";
                append_id_array(output, std::span{value.selected_workers});
                output += ",\"rack_count\":";
                append_u64(output, value.rack_count);
                output += ",\"nic_count\":";
                append_u64(output, value.nic_count);
                output += ",\"cross_rack_ring_edges\":";
                append_u64(output, value.cross_rack_ring_edges);
                output += ",\"reason\":";
                append_string(output, value.reason);
                output += ",\"outcome\":";
                append_string(output, placement_outcome_name(value.outcome));
            } else {
                static_assert(std::is_same_v<Observation, FailureObservation>);
                output += "{\"failure\":";
                append_u64(output, value.failure.value());
                output += ",\"transition\":";
                append_string(output, failure_transition_name(value.transition));
                output += ",\"resource_kind\":";
                append_string(output, failure_resource_name(value.resource_kind));
                output += ",\"resource_id\":";
                append_u64(output, value.resource_id);
                output += ",\"affected_bytes\":";
                append_u64(output, value.affected_bytes);
            }
            output.push_back('}');
        },
        observation);
}

void validate_serialization(const TelemetrySnapshot& snapshot,
                            const TelemetryRunMetadata& metadata) {
    validate_configuration(snapshot.configuration);
    if (!snapshot.finalized) {
        throw std::logic_error{"telemetry must be finalized before serialization"};
    }
    if (metadata.routing_policy.empty()) {
        throw std::invalid_argument{"telemetry routing policy metadata is required"};
    }
    if (!snapshot.records.empty() && snapshot.records.back().timestamp > metadata.final_time) {
        throw std::invalid_argument{"telemetry record exceeds run final time"};
    }
}

void enforce_serialized_limit(const TelemetrySnapshot& snapshot, const std::string& output) {
    if (output.size() > snapshot.configuration.limits.serialized_bytes) {
        throw std::length_error{"telemetry serialized byte limit exceeded"};
    }
}

} // namespace

std::string_view run_outcome_name(TelemetryRunOutcome outcome) {
    switch (outcome) {
    case TelemetryRunOutcome::Completed:
        return "completed";
    case TelemetryRunOutcome::Stopped:
        return "stopped";
    case TelemetryRunOutcome::Failed:
        return "failed";
    }
    throw std::invalid_argument{"unknown telemetry run outcome"};
}

void require_supported_schema_version(std::uint32_t version) {
    if (version != telemetry_schema_version) {
        throw std::invalid_argument{"unsupported telemetry schema version"};
    }
}

std::uint64_t fnv1a64(std::string_view content) noexcept {
    std::uint64_t digest = fnv_offset_basis;
    hash_text(digest, content);
    return digest;
}

std::string serialize_summary_json(const TelemetrySnapshot& snapshot,
                                   const TelemetryRunMetadata& metadata) {
    validate_serialization(snapshot, metadata);
    std::string output{"{"};
    append_common_metadata(output, "nexuslab.telemetry.summary", snapshot, metadata);
    output += ",\"metric_definitions\":[";
    const std::span definitions = metric_catalog();
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_metric_definition(output, definitions[index]);
    }
    output += "],\"metrics\":[";
    for (std::size_t index = 0; index < snapshot.metrics.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_metric(output, snapshot.metrics[index]);
    }
    output += "],\"job_attribution\":[";
    for (std::size_t index = 0; index < snapshot.job_attributions.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_attribution(output, snapshot.job_attributions[index]);
    }
    output += "]}\n";
    enforce_serialized_limit(snapshot, output);
    return output;
}

std::string serialize_records_jsonl(const TelemetrySnapshot& snapshot,
                                    const TelemetryRunMetadata& metadata) {
    validate_serialization(snapshot, metadata);
    std::string output{R"({"type":"header",)"};
    append_common_metadata(output, "nexuslab.telemetry.records", snapshot, metadata);
    output += "}\n";
    std::uint64_t digest = fnv_offset_basis;
    hash_text(digest, output);

    for (const TelemetryRecord& record : snapshot.records) {
        std::string line{R"({"type":"record","record_id":)"};
        append_u64(line, record.id.value());
        line += ",\"timestamp_ns\":";
        append_u64(line, record.timestamp.count());
        line += ",\"kind\":";
        append_string(line, record_kind_name(record_kind(record.observation)));
        line += ",\"correlation\":";
        append_correlation(line, record.correlation);
        line += ",\"observation\":";
        append_observation(line, record.observation);
        line += "}\n";
        hash_text(digest, line);
        output += line;
    }
    for (const MetricSample& sample : snapshot.samples) {
        std::string line{R"({"type":"sample","timestamp_ns":)"};
        append_u64(line, sample.timestamp.count());
        line += ",\"metric_id\":";
        append_u64(line, static_cast<std::uint8_t>(sample.metric));
        line += ",\"labels\":";
        append_labels(line, sample.labels);
        line += ",\"kind\":";
        append_string(line, kind_name(sample.kind));
        line += ",\"value\":";
        append_u64(line, sample.value);
        line += "}\n";
        hash_text(digest, line);
        output += line;
    }

    output += R"({"type":"footer","record_count":)";
    append_u64(output, snapshot.records.size());
    output += ",\"sample_count\":";
    append_u64(output, snapshot.samples.size());
    output += ",\"complete\":";
    append_bool(output, metadata.complete);
    output += ",\"content_digest_fnv1a64\":";
    append_string(output, hex_u64(digest));
    output += "}\n";
    enforce_serialized_limit(snapshot, output);
    return output;
}

} // namespace nexuslab::telemetry
