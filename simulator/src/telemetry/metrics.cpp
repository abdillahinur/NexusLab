// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/metrics.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace nexuslab::telemetry {
namespace {

constexpr MetricLabelMask outcome_label = label_mask(MetricLabel::Outcome);
constexpr MetricLabelMask link_labels =
    label_mask(MetricLabel::Link) | label_mask(MetricLabel::Direction);
constexpr MetricLabelMask policy_outcome_labels =
    label_mask(MetricLabel::Policy) | label_mask(MetricLabel::Outcome);
constexpr MetricLabelMask failure_labels =
    label_mask(MetricLabel::Failure) | label_mask(MetricLabel::Resource);

constexpr std::array<std::uint64_t, 9> latency_boundaries{
    1'000ULL,       10'000ULL,        100'000ULL,        1'000'000ULL,      10'000'000ULL,
    100'000'000ULL, 1'000'000'000ULL, 10'000'000'000ULL, 60'000'000'000ULL,
};
constexpr std::array<std::uint64_t, 14> count_boundaries{
    0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1'024, 2'048, 8'192,
};

constexpr std::array<MetricDefinition, 45> definitions{{
    {MetricId::SimulationDispatchedEvents, "simulation_dispatched_events_total",
     "Events dispatched by the simulation kernel", MetricKind::Counter, MetricUnit::Count},
    {MetricId::SimulationCancelledEvents, "simulation_cancelled_events_total",
     "Events cancelled by the simulation kernel", MetricKind::Counter, MetricUnit::Count},
    {MetricId::SimulationFinalTimeNs, "simulation_final_time_ns", "Final simulated timestamp",
     MetricKind::Gauge, MetricUnit::Nanoseconds},
    {MetricId::SimulationRngDraws, "simulation_rng_draws_total",
     "Deterministic random-number draws", MetricKind::Counter, MetricUnit::Count},
    {MetricId::SimulationTerminalTotal, "simulation_terminal_total", "Terminal simulation outcomes",
     MetricKind::Counter, MetricUnit::Count, outcome_label, outcome_label},
    {MetricId::JobCompletionTimeNs, "job_completion_time_ns", "Terminal job elapsed time",
     MetricKind::Histogram, MetricUnit::Nanoseconds, 0, 0, latency_boundaries},
    {MetricId::JobSchedulingWaitNs, "job_scheduling_wait_ns",
     "Arrival-to-allocation scheduling wait", MetricKind::Histogram, MetricUnit::Nanoseconds, 0, 0,
     latency_boundaries},
    {MetricId::JobComputeGpuNs, "job_compute_gpu_ns", "Allocated GPU compute resource time",
     MetricKind::Counter, MetricUnit::Nanoseconds},
    {MetricId::JobCommunicationNs, "job_communication_ns", "Job critical-path communication time",
     MetricKind::Counter, MetricUnit::Nanoseconds},
    {MetricId::JobSynchronizationWaitNs, "job_synchronization_wait_ns",
     "Job critical-path synchronization wait", MetricKind::Counter, MetricUnit::Nanoseconds},
    {MetricId::JobStragglerDelayNs, "job_straggler_delay_ns", "Configured worker straggler delay",
     MetricKind::Counter, MetricUnit::Nanoseconds},
    {MetricId::JobGpuIdleNs, "job_gpu_idle_ns", "Allocated idle GPU resource time",
     MetricKind::Counter, MetricUnit::Nanoseconds},
    {MetricId::JobCompletedSteps, "job_completed_steps", "Completed training steps",
     MetricKind::Counter, MetricUnit::Count},
    {MetricId::JobTerminalTotal, "job_terminal_total", "Terminal job outcomes", MetricKind::Counter,
     MetricUnit::Count, outcome_label, outcome_label},
    {MetricId::CollectiveDurationNs, "collective_duration_ns", "Terminal collective duration",
     MetricKind::Histogram, MetricUnit::Nanoseconds, 0, 0, latency_boundaries},
    {MetricId::CollectivePlannedBytes, "collective_planned_bytes_total",
     "Logical collective bytes planned", MetricKind::Counter, MetricUnit::Bytes},
    {MetricId::CollectiveIssuedFabricBytes, "collective_issued_fabric_bytes_total",
     "Collective bytes issued to the fabric", MetricKind::Counter, MetricUnit::Bytes},
    {MetricId::CollectiveIssuedLocalBytes, "collective_issued_local_bytes_total",
     "Collective bytes issued locally", MetricKind::Counter, MetricUnit::Bytes},
    {MetricId::CollectiveDeliveredBytes, "collective_delivered_bytes_total",
     "Collective bytes delivered", MetricKind::Counter, MetricUnit::Bytes},
    {MetricId::LinkAcceptedBytes, "link_accepted_bytes_total", "Bytes accepted by a link queue",
     MetricKind::Counter, MetricUnit::Bytes, link_labels, link_labels},
    {MetricId::LinkAcceptedChunks, "link_accepted_chunks_total", "Chunks accepted by a link queue",
     MetricKind::Counter, MetricUnit::Chunks, link_labels, link_labels},
    {MetricId::LinkSerializedBytes, "link_serialized_bytes_total", "Bytes fully serialized",
     MetricKind::Counter, MetricUnit::Bytes, link_labels, link_labels},
    {MetricId::LinkSerializedChunks, "link_serialized_chunks_total", "Chunks fully serialized",
     MetricKind::Counter, MetricUnit::Chunks, link_labels, link_labels},
    {MetricId::LinkMarkedBytes, "link_marked_bytes_total", "Bytes congestion marked at a link",
     MetricKind::Counter, MetricUnit::Bytes, link_labels, link_labels},
    {MetricId::LinkMarkedChunks, "link_marked_chunks_total", "Chunks congestion marked at a link",
     MetricKind::Counter, MetricUnit::Chunks, link_labels, link_labels},
    {MetricId::LinkDroppedBufferFullBytes, "link_dropped_buffer_full_bytes_total",
     "Bytes dropped because the waiting buffer was full", MetricKind::Counter, MetricUnit::Bytes,
     link_labels, link_labels},
    {MetricId::LinkDroppedBufferFullChunks, "link_dropped_buffer_full_chunks_total",
     "Chunks dropped because the waiting buffer was full", MetricKind::Counter, MetricUnit::Chunks,
     link_labels, link_labels},
    {MetricId::LinkDroppedDownBytes, "link_dropped_down_bytes_total",
     "Bytes dropped because a resource was down", MetricKind::Counter, MetricUnit::Bytes,
     link_labels, link_labels},
    {MetricId::LinkDroppedDownChunks, "link_dropped_down_chunks_total",
     "Chunks dropped because a resource was down", MetricKind::Counter, MetricUnit::Chunks,
     link_labels, link_labels},
    {MetricId::LinkQueueWaitingBytes, "link_queue_waiting_bytes", "Current waiting queue bytes",
     MetricKind::Gauge, MetricUnit::Bytes, link_labels, link_labels},
    {MetricId::LinkQueueWaitingChunks, "link_queue_waiting_chunks", "Current waiting queue chunks",
     MetricKind::Gauge, MetricUnit::Chunks, link_labels, link_labels},
    {MetricId::LinkMaximumWaitingBytes, "link_maximum_waiting_bytes",
     "Maximum observed waiting queue bytes", MetricKind::Gauge, MetricUnit::Bytes, link_labels,
     link_labels},
    {MetricId::LinkSerializerBusyNs, "link_serializer_busy_ns_total",
     "Elapsed link serializer busy time", MetricKind::Counter, MetricUnit::Nanoseconds, link_labels,
     link_labels},
    {MetricId::LinkUtilizationPpm, "link_utilization_ratio_ppm",
     "Link serializer utilization in parts per million", MetricKind::DerivedSummary,
     MetricUnit::RatioPartsPerMillion, link_labels, link_labels},
    {MetricId::TransferTerminalTotal, "transfer_terminal_total", "Terminal transfer outcomes",
     MetricKind::Counter, MetricUnit::Count, outcome_label, outcome_label},
    {MetricId::RoutingDecisionTotal, "routing_decision_total", "Routing decisions",
     MetricKind::Counter, MetricUnit::Count, policy_outcome_labels, policy_outcome_labels},
    {MetricId::PlacementDecisionTotal, "placement_decision_total", "Placement decisions",
     MetricKind::Counter, MetricUnit::Count, policy_outcome_labels, policy_outcome_labels},
    {MetricId::PlacementCrossRackRingEdges, "placement_cross_rack_ring_edges",
     "Cross-rack edges in the selected rank ring", MetricKind::Histogram, MetricUnit::Count, 0, 0,
     count_boundaries},
    {MetricId::PlacementRackCount, "placement_rack_count", "Racks used by a placement",
     MetricKind::Histogram, MetricUnit::Count, 0, 0, count_boundaries},
    {MetricId::PlacementNicCount, "placement_nic_count", "NICs used by a placement",
     MetricKind::Histogram, MetricUnit::Count, 0, 0, count_boundaries},
    {MetricId::FailureStateTransitionTotal, "failure_state_transition_total",
     "Failure resource-state transitions", MetricKind::Counter, MetricUnit::Count, failure_labels,
     failure_labels},
    {MetricId::FailureAffectedJobs, "failure_affected_jobs_total", "Jobs affected by failures",
     MetricKind::Counter, MetricUnit::Count, label_mask(MetricLabel::Failure), failure_labels},
    {MetricId::FailureAffectedTrafficBytes, "failure_affected_traffic_bytes_total",
     "Traffic bytes affected by failures", MetricKind::Counter, MetricUnit::Bytes,
     label_mask(MetricLabel::Failure), failure_labels},
    {MetricId::FailureDetectionTimeNs, "failure_detection_time_ns", "Failure detection latency",
     MetricKind::Histogram, MetricUnit::Nanoseconds, 0, label_mask(MetricLabel::Resource),
     latency_boundaries},
    {MetricId::FailureRecoveryTimeNs, "failure_recovery_time_ns", "Failure recovery latency",
     MetricKind::Histogram, MetricUnit::Nanoseconds, 0, label_mask(MetricLabel::Resource),
     latency_boundaries},
}};

constexpr MetricLabelMask known_label_mask =
    label_mask(MetricLabel::Job) | label_mask(MetricLabel::Collective) |
    label_mask(MetricLabel::Link) | label_mask(MetricLabel::Direction) |
    label_mask(MetricLabel::Policy) | label_mask(MetricLabel::Outcome) |
    label_mask(MetricLabel::Reason) | label_mask(MetricLabel::Failure) |
    label_mask(MetricLabel::Resource);

[[nodiscard]] bool valid_external_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '_' || name.back() == '_') {
        return false;
    }
    return std::ranges::all_of(name, [](char character) {
        const bool lowercase = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        return lowercase || digit || character == '_';
    });
}

[[nodiscard]] std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error{"telemetry metric value overflow"};
    }
    return left + right;
}

struct MetricSeriesKey final {
    MetricId metric;
    MetricLabels labels;

    auto operator<=>(const MetricSeriesKey&) const = default;
};

struct MetricSeriesState final {
    MetricKind kind;
    std::uint64_t scalar{0};
    std::vector<std::uint64_t> histogram_buckets;
    std::uint64_t histogram_count{0};
    std::uint64_t histogram_sum{0};
};

void validate_labels(const MetricDefinition& definition, const MetricLabels& labels) {
    const MetricLabelMask supplied = labels.mask();
    if ((supplied & definition.required_labels) != definition.required_labels) {
        throw std::invalid_argument{"telemetry metric is missing a required label"};
    }
    if ((supplied & ~definition.allowed_labels) != 0) {
        throw std::invalid_argument{"telemetry metric received an unsupported label"};
    }
}

} // namespace

std::string_view mode_name(TelemetryMode mode) {
    switch (mode) {
    case TelemetryMode::Off:
        return "off";
    case TelemetryMode::Summary:
        return "summary";
    case TelemetryMode::Sampled:
        return "sampled";
    case TelemetryMode::Full:
        return "full";
    }
    throw std::invalid_argument{"unknown telemetry mode"};
}

TelemetryMode parse_mode(std::string_view mode) {
    if (mode == "off") {
        return TelemetryMode::Off;
    }
    if (mode == "summary") {
        return TelemetryMode::Summary;
    }
    if (mode == "sampled") {
        return TelemetryMode::Sampled;
    }
    if (mode == "full") {
        return TelemetryMode::Full;
    }
    throw std::invalid_argument{"unknown telemetry mode"};
}

void validate_configuration(const TelemetryConfiguration& configuration) {
    static_cast<void>(mode_name(configuration.mode));
    if (configuration.sample_interval_ns == 0 || configuration.limits.metric_series == 0 ||
        configuration.limits.decision_records == 0 || configuration.limits.domain_records == 0 ||
        configuration.limits.samples == 0 || configuration.limits.histogram_boundaries == 0 ||
        configuration.limits.correlation_edges == 0 || configuration.limits.serialized_bytes == 0) {
        throw std::invalid_argument{"telemetry limits and sample interval must be positive"};
    }
    validate_metric_catalog(metric_catalog(), configuration.limits.histogram_boundaries);
}

std::string_view kind_name(MetricKind kind) {
    switch (kind) {
    case MetricKind::Counter:
        return "counter";
    case MetricKind::Gauge:
        return "gauge";
    case MetricKind::Histogram:
        return "histogram";
    case MetricKind::DerivedSummary:
        return "derived_summary";
    }
    throw std::invalid_argument{"unknown telemetry metric kind"};
}

std::string_view unit_name(MetricUnit unit) {
    switch (unit) {
    case MetricUnit::Count:
        return "count";
    case MetricUnit::Bytes:
        return "bytes";
    case MetricUnit::Chunks:
        return "chunks";
    case MetricUnit::Nanoseconds:
        return "nanoseconds";
    case MetricUnit::BitsPerSecond:
        return "bits_per_second";
    case MetricUnit::RatioPartsPerMillion:
        return "ratio_parts_per_million";
    }
    throw std::invalid_argument{"unknown telemetry metric unit"};
}

std::uint64_t policy_label_value(std::string_view policy) {
    if (policy == "ecmp") {
        return static_cast<std::uint64_t>(MetricPolicy::Ecmp);
    }
    if (policy == "shortest-path") {
        return static_cast<std::uint64_t>(MetricPolicy::ShortestPath);
    }
    if (policy == "least-loaded") {
        return static_cast<std::uint64_t>(MetricPolicy::LeastLoaded);
    }
    if (policy == "queue-aware") {
        return static_cast<std::uint64_t>(MetricPolicy::QueueAware);
    }
    if (policy == "first-fit") {
        return static_cast<std::uint64_t>(MetricPolicy::FirstFit);
    }
    if (policy == "random") {
        return static_cast<std::uint64_t>(MetricPolicy::Random);
    }
    if (policy == "rack-local") {
        return static_cast<std::uint64_t>(MetricPolicy::RackLocal);
    }
    if (policy == "compact") {
        return static_cast<std::uint64_t>(MetricPolicy::Compact);
    }
    return static_cast<std::uint64_t>(MetricPolicy::Extension);
}

MetricLabels::MetricLabels(std::initializer_list<MetricLabelValue> values) {
    if (values.size() > maximum_size) {
        throw std::length_error{"telemetry metric label limit exceeded"};
    }
    std::copy(values.begin(), values.end(), values_.begin());
    size_ = static_cast<std::uint8_t>(values.size());
    if (std::any_of(values_.begin(), values_.begin() + size_,
                    [](MetricLabelValue value) { return label_mask(value.label) == 0; })) {
        throw std::invalid_argument{"unknown telemetry metric label"};
    }
    std::sort(values_.begin(), values_.begin() + size_);
    const auto* const duplicate =
        std::adjacent_find(values_.begin(), values_.begin() + size_,
                           [](const MetricLabelValue& left, const MetricLabelValue& right) {
                               return left.label == right.label;
                           });
    if (duplicate != values_.begin() + size_) {
        throw std::invalid_argument{"duplicate telemetry metric label"};
    }
}

std::span<const MetricLabelValue> MetricLabels::values() const noexcept {
    return std::span{values_.data(), size_};
}

MetricLabelMask MetricLabels::mask() const noexcept {
    MetricLabelMask result = 0;
    for (const MetricLabelValue& value : values()) {
        result |= label_mask(value.label);
    }
    return result;
}

bool MetricLabels::operator==(const MetricLabels& other) const noexcept {
    return values().size() == other.values().size() &&
           std::equal(values().begin(), values().end(), other.values().begin());
}

std::strong_ordering MetricLabels::operator<=>(const MetricLabels& other) const noexcept {
    return std::lexicographical_compare_three_way(values().begin(), values().end(),
                                                  other.values().begin(), other.values().end());
}

std::span<const MetricDefinition> metric_catalog() noexcept { return definitions; }

const MetricDefinition& metric_definition(MetricId id) {
    const auto* const found =
        std::find_if(std::begin(definitions), std::end(definitions),
                     [id](const MetricDefinition& definition) { return definition.id == id; });
    if (found == std::end(definitions)) {
        throw std::invalid_argument{"unknown telemetry metric identifier"};
    }
    return *found;
}

void validate_metric_catalog(std::span<const MetricDefinition> catalog,
                             std::size_t maximum_histogram_boundaries) {
    if (catalog.empty()) {
        throw std::invalid_argument{"telemetry metric catalog must be nonempty"};
    }
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const MetricDefinition& definition = catalog[index];
        static_cast<void>(kind_name(definition.kind));
        static_cast<void>(unit_name(definition.unit));
        if (!valid_external_name(definition.name) || definition.description.empty()) {
            throw std::invalid_argument{"invalid telemetry metric definition text"};
        }
        if ((definition.required_labels & ~definition.allowed_labels) != 0 ||
            (definition.allowed_labels & ~known_label_mask) != 0) {
            throw std::invalid_argument{"invalid telemetry metric label mask"};
        }
        const bool histogram = definition.kind == MetricKind::Histogram;
        if (histogram != !definition.histogram_boundaries.empty()) {
            throw std::invalid_argument{"telemetry histogram boundaries do not match metric kind"};
        }
        if (definition.histogram_boundaries.size() > maximum_histogram_boundaries ||
            !std::is_sorted(definition.histogram_boundaries.begin(),
                            definition.histogram_boundaries.end()) ||
            std::adjacent_find(definition.histogram_boundaries.begin(),
                               definition.histogram_boundaries.end()) !=
                definition.histogram_boundaries.end()) {
            throw std::invalid_argument{"invalid telemetry histogram boundaries"};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (catalog[prior].id == definition.id || catalog[prior].name == definition.name) {
                throw std::invalid_argument{"duplicate telemetry metric definition"};
            }
        }
    }
}

struct MetricRegistry::Impl final {
    struct Limits final {
        std::size_t series;
        std::size_t histogram_boundaries;
    };

    explicit Impl(Limits limits) : maximum_series{limits.series} {
        if (maximum_series == 0 || limits.histogram_boundaries == 0) {
            throw std::invalid_argument{"telemetry metric catalog limits must be positive"};
        }
        validate_metric_catalog(metric_catalog(), limits.histogram_boundaries);
    }

    [[nodiscard]] MetricSeriesState& require_series(MetricId metric, const MetricLabels& labels,
                                                    MetricKind expected) {
        static_cast<void>(validate_series(metric, labels, expected));
        const MetricSeriesKey key{metric, labels};
        const auto found = series.find(key);
        if (found != series.end()) {
            return found->second;
        }

        MetricSeriesState state{expected, 0, {}, 0, 0};
        if (expected == MetricKind::Histogram) {
            const MetricDefinition& definition = metric_definition(metric);
            state.histogram_buckets.resize(definition.histogram_boundaries.size() + 1);
        }
        return series.emplace(key, std::move(state)).first->second;
    }

    [[nodiscard]] const MetricSeriesState*
    validate_series(MetricId metric, const MetricLabels& labels, MetricKind expected) const {
        const MetricDefinition& definition = metric_definition(metric);
        validate_labels(definition, labels);
        if (definition.kind != expected) {
            throw std::invalid_argument{"telemetry metric operation does not match metric kind"};
        }

        const MetricSeriesKey key{metric, labels};
        const auto found = series.find(key);
        if (found != series.end()) {
            return &found->second;
        }
        if (series.size() >= maximum_series) {
            throw std::length_error{"telemetry metric series limit exceeded"};
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<MetricSeriesSnapshot>
    find_snapshot(MetricId metric, const MetricLabels& labels) const {
        const auto found = series.find(MetricSeriesKey{metric, labels});
        if (found == series.end()) {
            return std::nullopt;
        }
        const MetricSeriesState& state = found->second;
        return MetricSeriesSnapshot{metric,
                                    labels,
                                    state.kind,
                                    state.scalar,
                                    state.histogram_buckets,
                                    state.histogram_count,
                                    state.histogram_sum};
    }

    std::size_t maximum_series;
    std::map<MetricSeriesKey, MetricSeriesState> series;
};

MetricRegistry::MetricRegistry(std::size_t maximum_series, std::size_t maximum_histogram_boundaries)
    : implementation_{
          std::make_unique<Impl>(Impl::Limits{maximum_series, maximum_histogram_boundaries})} {}

MetricRegistry::~MetricRegistry() = default;

void MetricRegistry::increment(MetricId metric, const MetricLabels& labels, std::uint64_t amount) {
    MetricSeriesState& series =
        implementation_->require_series(metric, labels, MetricKind::Counter);
    series.scalar = checked_add(series.scalar, amount);
}

void MetricRegistry::set_gauge(MetricId metric, const MetricLabels& labels, std::uint64_t value) {
    implementation_->require_series(metric, labels, MetricKind::Gauge).scalar = value;
}

void MetricRegistry::observe(MetricId metric, const MetricLabels& labels, std::uint64_t value) {
    MetricSeriesState& series =
        implementation_->require_series(metric, labels, MetricKind::Histogram);
    const MetricDefinition& definition = metric_definition(metric);
    const std::size_t bucket =
        static_cast<std::size_t>(std::lower_bound(definition.histogram_boundaries.begin(),
                                                  definition.histogram_boundaries.end(), value) -
                                 definition.histogram_boundaries.begin());
    const std::uint64_t next_bucket = checked_add(series.histogram_buckets[bucket], 1);
    const std::uint64_t next_count = checked_add(series.histogram_count, 1);
    const std::uint64_t next_sum = checked_add(series.histogram_sum, value);
    series.histogram_buckets[bucket] = next_bucket;
    series.histogram_count = next_count;
    series.histogram_sum = next_sum;
}

void MetricRegistry::validate_increment(MetricId metric, const MetricLabels& labels,
                                        std::uint64_t amount) const {
    const MetricSeriesState* const series =
        implementation_->validate_series(metric, labels, MetricKind::Counter);
    if (series != nullptr) {
        static_cast<void>(checked_add(series->scalar, amount));
    }
}

void MetricRegistry::validate_set_gauge(MetricId metric, const MetricLabels& labels,
                                        std::uint64_t value) const {
    static_cast<void>(value);
    static_cast<void>(implementation_->validate_series(metric, labels, MetricKind::Gauge));
}

void MetricRegistry::validate_observe(MetricId metric, const MetricLabels& labels,
                                      std::uint64_t value) const {
    const MetricSeriesState* const series =
        implementation_->validate_series(metric, labels, MetricKind::Histogram);
    if (series == nullptr) {
        return;
    }
    const MetricDefinition& definition = metric_definition(metric);
    const auto bucket =
        static_cast<std::size_t>(std::lower_bound(definition.histogram_boundaries.begin(),
                                                  definition.histogram_boundaries.end(), value) -
                                 definition.histogram_boundaries.begin());
    static_cast<void>(checked_add(series->histogram_buckets[bucket], 1));
    static_cast<void>(checked_add(series->histogram_count, 1));
    static_cast<void>(checked_add(series->histogram_sum, value));
}

std::optional<MetricSeriesSnapshot> MetricRegistry::find(MetricId metric,
                                                         const MetricLabels& labels) const {
    static_cast<void>(metric_definition(metric));
    return implementation_->find_snapshot(metric, labels);
}

std::vector<MetricSeriesSnapshot> MetricRegistry::snapshots() const {
    std::vector<MetricSeriesSnapshot> result;
    result.reserve(implementation_->series.size());
    for (const auto& [key, state] : implementation_->series) {
        result.push_back(MetricSeriesSnapshot{key.metric, key.labels, state.kind, state.scalar,
                                              state.histogram_buckets, state.histogram_count,
                                              state.histogram_sum});
    }
    return result;
}

std::size_t MetricRegistry::size() const noexcept { return implementation_->series.size(); }

} // namespace nexuslab::telemetry
