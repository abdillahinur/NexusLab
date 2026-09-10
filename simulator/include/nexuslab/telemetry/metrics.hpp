// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nexuslab::telemetry {

enum class TelemetryMode : std::uint8_t {
    Off = 0,
    Summary = 1,
    Sampled = 2,
    Full = 3,
};

[[nodiscard]] std::string_view mode_name(TelemetryMode mode);

struct TelemetryLimits final {
    std::size_t metric_series{100'000};
    std::size_t decision_records{100'000};
    std::size_t domain_records{1'000'000};
    std::size_t samples{1'000'000};
    std::size_t histogram_boundaries{64};
    std::size_t correlation_edges{4'000'000};
    std::uint64_t serialized_bytes{1'073'741'824};

    bool operator==(const TelemetryLimits&) const = default;
};

struct TelemetryConfiguration final {
    TelemetryMode mode{TelemetryMode::Summary};
    std::uint64_t sample_interval_ns{1'000'000};
    TelemetryLimits limits{};

    bool operator==(const TelemetryConfiguration&) const = default;
};

void validate_configuration(const TelemetryConfiguration& configuration);

enum class MetricKind : std::uint8_t {
    Counter = 1,
    Gauge = 2,
    Histogram = 3,
    DerivedSummary = 4,
};

enum class MetricUnit : std::uint8_t {
    Count = 1,
    Bytes = 2,
    Chunks = 3,
    Nanoseconds = 4,
    BitsPerSecond = 5,
    RatioPartsPerMillion = 6,
};

[[nodiscard]] std::string_view kind_name(MetricKind kind);
[[nodiscard]] std::string_view unit_name(MetricUnit unit);

enum class MetricLabel : std::uint8_t {
    Job = 1,
    Collective = 2,
    Link = 3,
    Direction = 4,
    Policy = 5,
    Outcome = 6,
    Reason = 7,
    Failure = 8,
    Resource = 9,
};

enum class MetricPolicy : std::uint16_t {
    Ecmp = 1,
    ShortestPath = 2,
    LeastLoaded = 3,
    QueueAware = 4,
    FirstFit = 10,
    Random = 11,
    RackLocal = 12,
    Compact = 13,
    Extension = 1'000,
};

[[nodiscard]] std::uint64_t policy_label_value(std::string_view policy);

using MetricLabelMask = std::uint32_t;

[[nodiscard]] constexpr MetricLabelMask label_mask(MetricLabel label) noexcept {
    const auto value = static_cast<std::uint8_t>(label);
    if (value < static_cast<std::uint8_t>(MetricLabel::Job) ||
        value > static_cast<std::uint8_t>(MetricLabel::Resource)) {
        return 0;
    }
    return MetricLabelMask{1} << (value - 1U);
}

struct MetricLabelValue final {
    MetricLabel label;
    std::uint64_t value;

    auto operator<=>(const MetricLabelValue&) const = default;
};

class MetricLabels final {
  public:
    static constexpr std::size_t maximum_size = 4;

    MetricLabels() = default;
    MetricLabels(std::initializer_list<MetricLabelValue> values);

    [[nodiscard]] std::span<const MetricLabelValue> values() const noexcept;
    [[nodiscard]] MetricLabelMask mask() const noexcept;

    [[nodiscard]] bool operator==(const MetricLabels& other) const noexcept;
    [[nodiscard]] std::strong_ordering operator<=>(const MetricLabels& other) const noexcept;

  private:
    std::array<MetricLabelValue, maximum_size> values_{};
    std::uint8_t size_{0};
};

enum class MetricId : std::uint8_t {
    SimulationDispatchedEvents = 1,
    SimulationCancelledEvents = 2,
    SimulationFinalTimeNs = 3,
    SimulationRngDraws = 4,
    SimulationTerminalTotal = 5,
    JobCompletionTimeNs = 10,
    JobSchedulingWaitNs = 11,
    JobComputeGpuNs = 12,
    JobCommunicationNs = 13,
    JobSynchronizationWaitNs = 14,
    JobStragglerDelayNs = 15,
    JobGpuIdleNs = 16,
    JobCompletedSteps = 17,
    JobTerminalTotal = 18,
    CollectiveDurationNs = 30,
    CollectivePlannedBytes = 31,
    CollectiveIssuedFabricBytes = 32,
    CollectiveIssuedLocalBytes = 33,
    CollectiveDeliveredBytes = 34,
    LinkAcceptedBytes = 50,
    LinkAcceptedChunks = 51,
    LinkSerializedBytes = 52,
    LinkSerializedChunks = 53,
    LinkMarkedBytes = 54,
    LinkMarkedChunks = 55,
    LinkDroppedBufferFullBytes = 56,
    LinkDroppedBufferFullChunks = 57,
    LinkDroppedDownBytes = 58,
    LinkDroppedDownChunks = 59,
    LinkQueueWaitingBytes = 60,
    LinkQueueWaitingChunks = 61,
    LinkMaximumWaitingBytes = 62,
    LinkSerializerBusyNs = 63,
    LinkUtilizationPpm = 64,
    TransferTerminalTotal = 70,
    RoutingDecisionTotal = 80,
    PlacementDecisionTotal = 81,
    PlacementCrossRackRingEdges = 82,
    PlacementRackCount = 83,
    PlacementNicCount = 84,
    FailureStateTransitionTotal = 100,
    FailureAffectedJobs = 101,
    FailureAffectedTrafficBytes = 102,
    FailureDetectionTimeNs = 103,
    FailureRecoveryTimeNs = 104,
};

struct MetricDefinition final {
    // Construction sites are the reviewed static catalog; parameter names make the adjacent text
    // and label-mask roles explicit.
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    constexpr MetricDefinition(MetricId metric_id, std::string_view metric_name,
                               std::string_view metric_description, MetricKind metric_kind,
                               MetricUnit metric_unit, MetricLabelMask required = 0,
                               MetricLabelMask allowed = 0,
                               std::span<const std::uint64_t> boundaries = {}) noexcept
        : name{metric_name}, description{metric_description}, histogram_boundaries{boundaries},
          required_labels{required}, allowed_labels{allowed}, id{metric_id}, kind{metric_kind},
          unit{metric_unit} {}
    // NOLINTEND(bugprone-easily-swappable-parameters)

    std::string_view name;
    std::string_view description;
    std::span<const std::uint64_t> histogram_boundaries;
    MetricLabelMask required_labels{0};
    MetricLabelMask allowed_labels{0};
    MetricId id;
    MetricKind kind;
    MetricUnit unit;
};

[[nodiscard]] std::span<const MetricDefinition> metric_catalog() noexcept;
[[nodiscard]] const MetricDefinition& metric_definition(MetricId id);
void validate_metric_catalog(std::span<const MetricDefinition> catalog,
                             std::size_t maximum_histogram_boundaries);

struct MetricSeriesSnapshot final {
    MetricId metric;
    MetricLabels labels;
    MetricKind kind;
    std::uint64_t scalar{0};
    std::vector<std::uint64_t> histogram_buckets;
    std::uint64_t histogram_count{0};
    std::uint64_t histogram_sum{0};

    bool operator==(const MetricSeriesSnapshot&) const = default;
};

class MetricRegistry final {
  public:
    explicit MetricRegistry(std::size_t maximum_series = TelemetryLimits{}.metric_series);
    ~MetricRegistry();
    MetricRegistry(const MetricRegistry&) = delete;
    MetricRegistry& operator=(const MetricRegistry&) = delete;
    MetricRegistry(MetricRegistry&&) = delete;
    MetricRegistry& operator=(MetricRegistry&&) = delete;

    void increment(MetricId metric, const MetricLabels& labels = {}, std::uint64_t amount = 1);
    void set_gauge(MetricId metric, const MetricLabels& labels, std::uint64_t value);
    void observe(MetricId metric, const MetricLabels& labels, std::uint64_t value);

    void validate_increment(MetricId metric, const MetricLabels& labels = {},
                            std::uint64_t amount = 1) const;
    void validate_set_gauge(MetricId metric, const MetricLabels& labels, std::uint64_t value) const;
    void validate_observe(MetricId metric, const MetricLabels& labels, std::uint64_t value) const;

    [[nodiscard]] std::optional<MetricSeriesSnapshot> find(MetricId metric,
                                                           const MetricLabels& labels = {}) const;
    [[nodiscard]] std::vector<MetricSeriesSnapshot> snapshots() const;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

} // namespace nexuslab::telemetry
