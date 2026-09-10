// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/collective/model.hpp"
#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/sim/time.hpp"
#include "nexuslab/telemetry/metrics.hpp"
#include "nexuslab/topology/entities.hpp"
#include "nexuslab/transport/types.hpp"
#include "nexuslab/workload/events.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace nexuslab::telemetry {

struct TelemetryRecordIdTag final {};
struct RoutingDecisionIdTag final {};
struct PlacementDecisionIdTag final {};
struct FailureIdTag final {};

using TelemetryRecordId = topology::StrongId<TelemetryRecordIdTag>;
using RoutingDecisionId = topology::StrongId<RoutingDecisionIdTag>;
using PlacementDecisionId = topology::StrongId<PlacementDecisionIdTag>;
using FailureId = topology::StrongId<FailureIdTag>;

struct Correlation final {
    std::optional<topology::GpuId> gpu;
    std::optional<topology::NicId> nic;
    std::optional<topology::SwitchId> switch_entity;
    std::optional<topology::PortId> port;
    std::optional<topology::RackId> rack;
    std::optional<topology::DirectedLinkId> link;
    std::optional<sim::EventId> event;
    std::optional<sim::EventId> cause;
    std::optional<workload::JobId> job;
    std::optional<collective::CollectiveId> collective;
    std::optional<transport::TransferId> transfer;
    std::optional<transport::ChunkId> chunk;
    std::optional<RoutingDecisionId> routing_decision;
    std::optional<PlacementDecisionId> placement_decision;
    std::optional<FailureId> failure;

    bool operator==(const Correlation&) const = default;
};

[[nodiscard]] std::size_t correlation_edge_count(const Correlation& correlation) noexcept;

struct CounterObservation final {
    MetricId metric;
    MetricLabels labels;
    std::uint64_t amount;

    bool operator==(const CounterObservation&) const = default;
};

struct GaugeObservation final {
    MetricId metric;
    MetricLabels labels;
    std::uint64_t value;

    bool operator==(const GaugeObservation&) const = default;
};

struct HistogramObservation final {
    MetricId metric;
    MetricLabels labels;
    std::uint64_t value;

    bool operator==(const HistogramObservation&) const = default;
};

using MetricObservation = std::variant<CounterObservation, GaugeObservation, HistogramObservation>;

enum class SimulationTransition : std::uint8_t {
    RunStarted = 1,
    EventScheduled = 2,
    EventDispatched = 3,
    EventCancelled = 4,
    StopRequested = 5,
    RunSucceeded = 6,
    RunFailed = 7,
    RunStopped = 8,
};

struct SimulationObservation final {
    SimulationTransition transition;
    std::uint64_t pending_events;

    bool operator==(const SimulationObservation&) const = default;
};

enum class JobTransition : std::uint8_t {
    Arrived = 1,
    Waiting = 2,
    Admitted = 3,
    ComputeStarted = 4,
    ComputeCompleted = 5,
    CollectiveStarted = 6,
    CollectiveCompleted = 7,
    StepCompleted = 8,
    Succeeded = 9,
    Failed = 10,
    Cancelled = 11,
};

struct JobObservation final {
    JobTransition transition;
    std::uint32_t step;
    std::uint32_t bucket;
    std::uint32_t worker;
    std::uint32_t allocated_workers;

    bool operator==(const JobObservation&) const = default;
};

enum class CollectiveTransition : std::uint8_t {
    Submitted = 1,
    PhaseStarted = 2,
    RoundStarted = 3,
    RoundCompleted = 4,
    TransferIssued = 5,
    Succeeded = 6,
    Failed = 7,
    Cancelled = 8,
};

struct CollectiveObservation final {
    CollectiveTransition transition;
    std::uint32_t phase;
    std::uint32_t round;
    std::uint64_t bytes;

    bool operator==(const CollectiveObservation&) const = default;
};

enum class TransferTransition : std::uint8_t {
    Submitted = 1,
    ChunkQueued = 2,
    ChunkServiceStarted = 3,
    ChunkSerialized = 4,
    ChunkDelivered = 5,
    ChunkDropped = 6,
    Succeeded = 7,
    Failed = 8,
};

enum class TransferReason : std::uint8_t {
    None = 0,
    BufferFull = 1,
    ResourceDown = 2,
};

struct TransferObservation final {
    TransferTransition transition;
    std::uint64_t bytes;
    std::uint32_t hop;
    TransferReason reason;

    bool operator==(const TransferObservation&) const = default;
};

enum class QueueTransition : std::uint8_t {
    Enqueued = 1,
    ServiceStarted = 2,
    ServiceCompleted = 3,
    Marked = 4,
    Dropped = 5,
};

struct QueueObservation final {
    QueueTransition transition;
    std::uint64_t chunk_bytes;
    std::uint64_t waiting_bytes;
    std::uint64_t waiting_chunks;

    bool operator==(const QueueObservation&) const = default;
};

enum class RoutingDecisionOutcome : std::uint8_t {
    Selected = 1,
    NoRoute = 2,
    Rejected = 3,
};

struct RoutingDecisionObservation final {
    RoutingDecisionId decision;
    std::string policy;
    std::uint64_t policy_version;
    std::uint64_t operational_revision;
    std::uint64_t candidate_count;
    std::vector<topology::DirectedLinkId> selected_path;
    std::uint64_t score;
    std::string reason;
    std::optional<transport::TransferId> transfer;
    RoutingDecisionOutcome outcome;

    bool operator==(const RoutingDecisionObservation&) const = default;
};

enum class PlacementDecisionOutcome : std::uint8_t {
    Placed = 1,
    Waiting = 2,
    Rejected = 3,
};

struct PlacementDecisionObservation final {
    PlacementDecisionId decision;
    std::string policy;
    std::uint64_t policy_version;
    std::uint32_t priority;
    std::uint32_t requested_workers;
    std::vector<topology::GpuId> selected_workers;
    std::uint64_t rack_count;
    std::uint64_t nic_count;
    std::uint64_t cross_rack_ring_edges;
    std::string reason;
    PlacementDecisionOutcome outcome;

    bool operator==(const PlacementDecisionObservation&) const = default;
};

enum class FailureTransition : std::uint8_t {
    Scheduled = 1,
    Detected = 2,
    Applied = 3,
    RecoveryStarted = 4,
    Recovered = 5,
};

enum class FailureResourceKind : std::uint8_t {
    Link = 1,
    Port = 2,
    Switch = 3,
    Gpu = 4,
    Nic = 5,
};

struct FailureObservation final {
    FailureId failure;
    FailureTransition transition;
    FailureResourceKind resource_kind;
    std::uint64_t resource_id;
    std::uint64_t affected_bytes;

    bool operator==(const FailureObservation&) const = default;
};

using TelemetryObservation =
    std::variant<CounterObservation, GaugeObservation, HistogramObservation, SimulationObservation,
                 JobObservation, CollectiveObservation, TransferObservation, QueueObservation,
                 RoutingDecisionObservation, PlacementDecisionObservation, FailureObservation>;

enum class TelemetryRecordKind : std::uint8_t {
    Counter = 1,
    Gauge = 2,
    Histogram = 3,
    Simulation = 10,
    Job = 11,
    Collective = 12,
    Transfer = 13,
    Queue = 14,
    Failure = 15,
    RoutingDecision = 20,
    PlacementDecision = 21,
};

[[nodiscard]] TelemetryRecordKind record_kind(const TelemetryObservation& observation);
[[nodiscard]] bool is_metric_observation(const TelemetryObservation& observation) noexcept;
[[nodiscard]] bool is_decision_observation(const TelemetryObservation& observation) noexcept;

struct TelemetryRecord final {
    Correlation correlation;
    TelemetryObservation observation;
    TelemetryRecordId id;
    sim::SimTimeNs timestamp;

    bool operator==(const TelemetryRecord&) const = default;
};

} // namespace nexuslab::telemetry
