// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/runtime.hpp"
#include "nexuslab/transport/timing.hpp"
#include <stdexcept>
#include <utility>

namespace nexuslab::transport {

void TransportRuntime::validate_capacity(std::size_t count, std::size_t hops) const {
    if (hops > limits_.maximum_route_hops || count > limits_.maximum_chunks - chunks_.size() ||
        (hops != 0 && count > (limits_.maximum_route_entries - route_entries_) / hops)) {
        throw std::length_error{"transport retained-state limit exceeded"};
    }
}

void TransportRuntime::validate_timing(ByteCount bytes,
                                       std::span<const topology::DirectedLinkId> route,
                                       sim::SimTimeNs now) const {
    for (const auto link : route) {
        const auto* service = find_service(link);
        if (service == nullptr) {
            throw std::invalid_argument{"route uses an unconfigured link"};
        }
        const auto& config = service->queue().configuration();
        const auto delay = serialization_delay(bytes, config.bandwidth);
        if (!delay.has_value()) {
            throw std::overflow_error{"serialization delay exceeds simulated-time range"};
        }
        const auto serialized = sim::checked_add(now, *delay);
        const auto propagated = serialized.has_value()
                                    ? sim::checked_add(*serialized, config.propagation_delay)
                                    : std::nullopt;
        if (!propagated.has_value()) {
            throw std::overflow_error{"route timing exceeds simulated-time range"};
        }
        now = *propagated;
    }
}

void TransportRuntime::record_terminal(const ChunkRecord& record, sim::SimTimeNs now) {
    auto& transfer = transfers_.at(record.chunk.transfer);
    if (transfer.completion.has_value()) {
        throw std::logic_error{"transfer already completed"};
    }
    const TrafficCount count{record.chunk.bytes.value(), 1};
    switch (record.state) {
    case ChunkTransitState::Delivered:
        transfer.delivered = add_traffic(transfer.delivered, count);
        break;
    case ChunkTransitState::DroppedBufferFull:
        transfer.dropped_buffer_full = add_traffic(transfer.dropped_buffer_full, count);
        break;
    case ChunkTransitState::DroppedLinkDown:
        transfer.dropped_link_down = add_traffic(transfer.dropped_link_down, count);
        break;
    default:
        throw std::logic_error{"nonterminal chunk cannot finish a transfer"};
    }
    const auto drops = add_traffic(transfer.dropped_buffer_full, transfer.dropped_link_down);
    const auto terminal = add_traffic(transfer.delivered, drops);
    if (terminal.chunks == transfer.total.chunks) {
        if (terminal.bytes != transfer.total.bytes) {
            throw std::logic_error{"terminal transfer byte accounting mismatch"};
        }
        const TransferCompletion completion{record.chunk.transfer,
                                            drops.chunks == 0 ? TransferOutcome::Succeeded
                                                              : TransferOutcome::Failed,
                                            now,
                                            transfer.delivered,
                                            transfer.dropped_buffer_full,
                                            transfer.dropped_link_down};
        completions_.push_back(completion);
        transfer.completion = completion;
    }
}

std::optional<TransferSnapshot> TransportRuntime::transfer_snapshot(TransferId id) const {
    const auto iterator = transfers_.find(id);
    if (iterator == transfers_.end()) {
        return std::nullopt;
    }
    const auto& transfer = iterator->second;
    TransferSnapshot snapshot{};
    snapshot.total = transfer.total;
    snapshot.started_at = transfer.started_at;
    snapshot.completion = transfer.completion;
    snapshot.delivered = transfer.delivered;
    snapshot.dropped_buffer_full = transfer.dropped_buffer_full;
    snapshot.dropped_link_down = transfer.dropped_link_down;
    for (const auto chunk : transfer.chunks) {
        const auto& record = chunks_.at(chunk);
        const TrafficCount count{record.chunk.bytes.value(), 1};
        switch (record.state) {
        case ChunkTransitState::Registered:
            snapshot.registered = add_traffic(snapshot.registered, count);
            break;
        case ChunkTransitState::AwaitingArrival:
            if (record.hop_index == 0) {
                snapshot.awaiting_initial_arrival =
                    add_traffic(snapshot.awaiting_initial_arrival, count);
            } else {
                snapshot.propagating = add_traffic(snapshot.propagating, count);
            }
            break;
        case ChunkTransitState::Admitted: {
            const auto& service = services_.at(record.route.at(record.hop_index));
            const auto* active = service.queue().active();
            auto& category =
                active != nullptr && active->id == chunk ? snapshot.active : snapshot.waiting;
            category = add_traffic(category, count);
            break;
        }
        case ChunkTransitState::Delivered:
        case ChunkTransitState::DroppedBufferFull:
        case ChunkTransitState::DroppedLinkDown:
            break;
        }
    }
    return snapshot;
}

std::vector<TransferCompletion> TransportRuntime::take_completed_transfers() {
    return std::exchange(completions_, {});
}

} // namespace nexuslab::transport
