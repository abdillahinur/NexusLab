// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/runtime.hpp"

#include "nexuslab/sim/simulation.hpp"

#include <stdexcept>

namespace nexuslab::transport {
namespace {

[[nodiscard]] telemetry::MetricLabels link_labels(topology::DirectedLinkId link) {
    return {{telemetry::MetricLabel::Link, link.link.value()},
            {telemetry::MetricLabel::Direction, static_cast<std::uint64_t>(link.direction)}};
}

[[nodiscard]] telemetry::Correlation event_correlation(sim::SimulationContext& context) {
    telemetry::Correlation correlation;
    correlation.event = context.current_event_id();
    correlation.cause = context.cause();
    return correlation;
}

[[nodiscard]] std::uint64_t counter_delta(std::uint64_t before, std::uint64_t after) {
    if (after < before) {
        throw std::logic_error{"transport telemetry counter regressed"};
    }
    return after - before;
}

struct TrafficMetricPair final {
    telemetry::MetricId bytes;
    telemetry::MetricId chunks;
};

} // namespace

void TransportRuntime::emit_transfer(const ChunkRecord& record,
                                     telemetry::TransferTransition transition,
                                     sim::SimulationContext& context,
                                     std::optional<topology::DirectedLinkId> link,
                                     telemetry::TransferReason reason) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation = event_correlation(context);
    correlation.transfer = record.chunk.transfer;
    correlation.chunk = record.chunk.id;
    correlation.link = link;
    telemetry_.record(context.now(), correlation,
                      telemetry::TransferObservation{transition, record.chunk.bytes.value(),
                                                     record.hop_index, reason});
}

void TransportRuntime::emit_queue(const ChunkRecord& record, topology::DirectedLinkId link,
                                  telemetry::QueueTransition transition,
                                  sim::SimulationContext& context) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation = event_correlation(context);
    correlation.transfer = record.chunk.transfer;
    correlation.chunk = record.chunk.id;
    correlation.link = link;
    const QueueSnapshot snapshot = require_service(link).queue().snapshot();
    telemetry_.record(context.now(), correlation,
                      telemetry::QueueObservation{transition, record.chunk.bytes.value(),
                                                  snapshot.waiting_bytes.value(),
                                                  snapshot.waiting_chunks});
}

void TransportRuntime::emit_link_metrics(topology::DirectedLinkId link,
                                         const LinkStatistics& before, const LinkStatistics& after,
                                         sim::SimulationContext& context) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation = event_correlation(context);
    correlation.link = link;
    const telemetry::MetricLabels labels = link_labels(link);
    const auto emit_delta = [this, &context, &correlation, &labels](TrafficMetricPair metrics,
                                                                    TrafficCount earlier,
                                                                    TrafficCount later) {
        const std::uint64_t bytes = counter_delta(earlier.bytes, later.bytes);
        const std::uint64_t chunks = counter_delta(earlier.chunks, later.chunks);
        if (bytes != 0) {
            telemetry_.record(context.now(), correlation,
                              telemetry::CounterObservation{metrics.bytes, labels, bytes});
        }
        if (chunks != 0) {
            telemetry_.record(context.now(), correlation,
                              telemetry::CounterObservation{metrics.chunks, labels, chunks});
        }
    };

    emit_delta({telemetry::MetricId::LinkAcceptedBytes, telemetry::MetricId::LinkAcceptedChunks},
               before.enqueued, after.enqueued);
    emit_delta(
        {telemetry::MetricId::LinkSerializedBytes, telemetry::MetricId::LinkSerializedChunks},
        before.completed, after.completed);
    emit_delta({telemetry::MetricId::LinkMarkedBytes, telemetry::MetricId::LinkMarkedChunks},
               before.marked, after.marked);
    emit_delta({telemetry::MetricId::LinkDroppedBufferFullBytes,
                telemetry::MetricId::LinkDroppedBufferFullChunks},
               before.dropped_buffer_full, after.dropped_buffer_full);
    emit_delta(
        {telemetry::MetricId::LinkDroppedDownBytes, telemetry::MetricId::LinkDroppedDownChunks},
        before.dropped_link_down, after.dropped_link_down);

    std::uint64_t& reported = reported_busy_time_ns_.at(link);
    const std::uint64_t busy = after.busy_time.count();
    const std::uint64_t elapsed = counter_delta(reported, busy);
    if (elapsed != 0) {
        telemetry_.record(context.now(), correlation,
                          telemetry::CounterObservation{telemetry::MetricId::LinkSerializerBusyNs,
                                                        labels, elapsed});
    }
    reported = busy;
}

void TransportRuntime::emit_queue_gauges(topology::DirectedLinkId link,
                                         const QueueSnapshot& snapshot,
                                         sim::SimulationContext& context) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation = event_correlation(context);
    correlation.link = link;
    const telemetry::MetricLabels labels = link_labels(link);
    telemetry_.record(context.now(), correlation,
                      telemetry::GaugeObservation{telemetry::MetricId::LinkQueueWaitingBytes,
                                                  labels, snapshot.waiting_bytes.value()});
    telemetry_.record(context.now(), correlation,
                      telemetry::GaugeObservation{telemetry::MetricId::LinkQueueWaitingChunks,
                                                  labels, snapshot.waiting_chunks});
    telemetry_.record(context.now(), correlation,
                      telemetry::GaugeObservation{telemetry::MetricId::LinkMaximumWaitingBytes,
                                                  labels, snapshot.maximum_waiting_bytes.value()});
}

} // namespace nexuslab::transport
