// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/transport/runtime.hpp"

#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/sim/time.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace nexuslab::transport {

TransportRuntime::TransportRuntime(topology::TopologyGraph& topology,
                                   const std::vector<DirectedLinkConfiguration>& configurations)
    : topology_{&topology} {
    for (const DirectedLinkConfiguration& configuration : configurations) {
        static_cast<void>(require_fabric_arc(configuration.link));
        const auto [iterator, inserted] = services_.try_emplace(configuration.link, configuration);
        static_cast<void>(iterator);
        if (!inserted) {
            throw std::invalid_argument{"duplicate directed-link transport configuration"};
        }
    }
}

void TransportRuntime::register_chunk(RoutedChunk routed_chunk) {
    if (routed_chunk.chunk.bytes.value() == 0) {
        throw std::invalid_argument{"routed chunk must contain nonzero bytes"};
    }
    if (routed_chunk.chunk.hop_index != 0) {
        throw std::invalid_argument{"new routed chunk must begin at hop zero"};
    }
    validate_route(routed_chunk.route, std::nullopt, std::nullopt);

    const ChunkId id = routed_chunk.chunk.id;
    const TransferId transfer = routed_chunk.chunk.transfer;
    const auto [iterator, inserted] =
        chunks_.try_emplace(id, ChunkRecord{routed_chunk.chunk, std::move(routed_chunk.route),
                                            ChunkTransitState::Registered, 0, std::nullopt});
    static_cast<void>(iterator);
    if (!inserted) {
        throw std::invalid_argument{"duplicate routed chunk identifier"};
    }
    transfer_ids_.advance_past(transfer);
    chunk_ids_.advance_past(id);
}

SubmittedTransfer TransportRuntime::submit_transfer(const TransferRequest& request,
                                                    sim::SimulationContext& context) {
    if (request.bytes.value() == 0) {
        throw std::invalid_argument{"transfer byte count must be nonzero"};
    }
    if (request.maximum_chunk_bytes.value() == 0) {
        throw std::invalid_argument{"maximum chunk byte count must be nonzero"};
    }
    validate_route(request.route, request.source, request.destination);

    const std::uint64_t full_chunks = request.bytes.value() / request.maximum_chunk_bytes.value();
    const std::uint64_t remainder = request.bytes.value() % request.maximum_chunk_bytes.value();
    const std::uint64_t chunk_count = full_chunks + static_cast<std::uint64_t>(remainder != 0);
    if (!transfer_ids_.can_generate(1) || !chunk_ids_.can_generate(chunk_count)) {
        throw std::overflow_error{"transport ID sequence cannot represent submitted transfer"};
    }
    if (chunk_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::overflow_error{"chunk count exceeds runtime container range"};
    }

    std::vector<ByteCount> partitions;
    partitions.reserve(static_cast<std::size_t>(chunk_count));
    for (std::uint64_t ordinal = 0; ordinal < full_chunks; ++ordinal) {
        partitions.push_back(request.maximum_chunk_bytes);
    }
    if (remainder != 0) {
        partitions.emplace_back(remainder);
    }

    const TransferId transfer = transfer_ids_.next();
    SubmittedTransfer submitted{transfer, {}};
    submitted.chunks.reserve(partitions.size());
    for (const ByteCount bytes : partitions) {
        const ChunkId chunk = chunk_ids_.next();
        register_chunk(RoutedChunk{
            TransferChunk{transfer, chunk, bytes, 0, false},
            request.route,
        });
        static_cast<void>(schedule_initial_arrival(chunk, context));
        submitted.chunks.push_back(SubmittedChunk{chunk, bytes});
    }
    return submitted;
}

sim::EventId TransportRuntime::schedule_initial_arrival(ChunkId chunk,
                                                        sim::SimulationContext& context) {
    ChunkRecord& record = require_chunk(chunk);
    if (record.state != ChunkTransitState::Registered) {
        throw std::logic_error{"routed chunk initial arrival is already scheduled"};
    }

    const sim::EventId event_id = context.schedule(sim::EventSpec{
        context.now(),
        sim::EventPriority::Normal,
        sim::EventPayload{ChunkArrivalEvent{chunk, 0}},
    });
    record.state = ChunkTransitState::AwaitingArrival;
    record.scheduled_arrival = event_id;
    return event_id;
}

sim::EventId TransportRuntime::schedule_link_state_change(topology::LinkId link,
                                                          topology::OperationalState state,
                                                          sim::SimTimeNs timestamp,
                                                          sim::SimulationContext& context) {
    const topology::PhysicalLink* physical = topology_->find(link);
    if (physical == nullptr || physical->kind != topology::LinkKind::Fabric) {
        throw std::invalid_argument{"state change must identify a known fabric link"};
    }
    switch (state) {
    case topology::OperationalState::Up:
    case topology::OperationalState::Down:
        break;
    default:
        throw std::invalid_argument{"link operational state is invalid"};
    }
    return context.schedule(sim::EventSpec{
        timestamp,
        sim::EventPriority::Critical,
        sim::EventPayload{LinkStateChangeEvent{link, state}},
    });
}

void TransportRuntime::handle_arrival(const ChunkArrivalEvent& event,
                                      sim::SimulationContext& context) {
    ChunkRecord& record = require_chunk(event.chunk);
    if (record.state != ChunkTransitState::AwaitingArrival ||
        !record.scheduled_arrival.has_value() ||
        *record.scheduled_arrival != context.current_event_id() ||
        event.hop_index != record.hop_index) {
        throw std::logic_error{"unexpected chunk-arrival event"};
    }

    record.scheduled_arrival.reset();
    if (static_cast<std::size_t>(event.hop_index) == record.route.size()) {
        record.state = ChunkTransitState::Delivered;
        return;
    }

    const topology::DirectedLinkId link = record.route[event.hop_index];
    const topology::DirectedLink directed = require_fabric_arc(link);
    if (!topology_->is_operational(directed)) {
        record.state = ChunkTransitState::DroppedLinkDown;
        return;
    }

    record.chunk.hop_index = event.hop_index;
    const AdmissionResult admission = require_service(link).admit(record.chunk, context);
    record.chunk.marked = record.chunk.marked || admission.marked_here;
    record.state = admission.disposition == AdmissionDisposition::DroppedBufferFull
                       ? ChunkTransitState::DroppedBufferFull
                       : ChunkTransitState::Admitted;
}

void TransportRuntime::handle_completion(const TransmissionCompleteEvent& event,
                                         sim::SimulationContext& context) {
    const topology::DirectedLinkId link{event.link, event.direction};
    ChunkRecord& record = require_chunk(event.chunk);
    if (record.state != ChunkTransitState::Admitted || event.hop_index != record.hop_index ||
        static_cast<std::size_t>(event.hop_index) >= record.route.size() ||
        record.route[event.hop_index] != link) {
        throw std::logic_error{"unexpected routed transmission-completion event"};
    }

    DirectedLinkService& service = require_service(link);
    const auto arrival_time =
        sim::checked_add(context.now(), service.queue().configuration().propagation_delay);
    if (!arrival_time.has_value()) {
        throw std::overflow_error{"propagation arrival exceeds simulated-time range"};
    }
    const sim::SimTimeNs validated_arrival_time = arrival_time.value();

    const ServiceTransition transition = service.handle_completion(event, context);
    if (transition.completed.id != record.chunk.id) {
        throw std::logic_error{"completed service chunk does not match routed chunk"};
    }

    record.chunk = transition.completed;
    ++record.hop_index;
    const sim::EventId arrival = context.schedule(sim::EventSpec{
        validated_arrival_time,
        sim::EventPriority::Normal,
        sim::EventPayload{ChunkArrivalEvent{record.chunk.id, record.hop_index}},
    });
    record.state = ChunkTransitState::AwaitingArrival;
    record.scheduled_arrival = arrival;
}

void TransportRuntime::handle_link_state_change(const LinkStateChangeEvent& event,
                                                sim::SimulationContext& context) {
    const topology::PhysicalLink* physical = topology_->find(event.link);
    if (physical == nullptr || physical->kind != topology::LinkKind::Fabric) {
        throw std::invalid_argument{"state change must identify a known fabric link"};
    }
    switch (event.state) {
    case topology::OperationalState::Up:
    case topology::OperationalState::Down:
        break;
    default:
        throw std::invalid_argument{"link operational state is invalid"};
    }
    if (!topology_->set_link_state(event.link, event.state)) {
        throw std::logic_error{"known fabric link state could not be changed"};
    }
    if (event.state == topology::OperationalState::Up) {
        return;
    }

    for (const topology::LinkDirection direction :
         {topology::LinkDirection::AToB, topology::LinkDirection::BToA}) {
        const topology::DirectedLinkId directed{event.link, direction};
        const auto service = services_.find(directed);
        if (service == services_.end()) {
            continue;
        }
        const QueueDrain drained = service->second.reconcile_down(context);
        mark_dropped_link_down(drained, directed);
    }
}

std::optional<ChunkTransitSnapshot> TransportRuntime::chunk_snapshot(ChunkId chunk) const noexcept {
    const auto iterator = chunks_.find(chunk);
    if (iterator == chunks_.end()) {
        return std::nullopt;
    }
    const ChunkRecord& record = iterator->second;
    return ChunkTransitSnapshot{record.state, record.hop_index, record.chunk.marked};
}

const DirectedLinkService*
TransportRuntime::find_service(topology::DirectedLinkId link) const noexcept {
    const auto iterator = services_.find(link);
    return iterator == services_.end() ? nullptr : &iterator->second;
}

topology::DirectedLink TransportRuntime::require_fabric_arc(topology::DirectedLinkId link) const {
    const topology::PhysicalLink* physical = topology_->find(link.link);
    if (physical == nullptr || physical->kind != topology::LinkKind::Fabric) {
        throw std::invalid_argument{"transport link must identify a known fabric arc"};
    }
    const auto directions = topology::directed_links(*physical);
    switch (link.direction) {
    case topology::LinkDirection::AToB:
        return directions[0];
    case topology::LinkDirection::BToA:
        return directions[1];
    }
    throw std::invalid_argument{"transport link direction is invalid"};
}

void TransportRuntime::validate_route(std::span<const topology::DirectedLinkId> route,
                                      std::optional<topology::NodeId> expected_source,
                                      std::optional<topology::NodeId> expected_destination) const {
    if (route.empty()) {
        throw std::invalid_argument{"routed chunk path must be nonempty"};
    }
    if (route.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::overflow_error{"routed chunk path exceeds hop-index range"};
    }

    const topology::DirectedLink first = require_fabric_arc(route.front());
    if (find_service(route.front()) == nullptr) {
        throw std::invalid_argument{"routed chunk path uses an unconfigured directed link"};
    }
    topology::DirectedLink previous = first;
    for (std::size_t index = 1; index < route.size(); ++index) {
        const topology::DirectedLinkId link = route[index];
        const topology::DirectedLink directed = require_fabric_arc(link);
        if (find_service(link) == nullptr) {
            throw std::invalid_argument{"routed chunk path uses an unconfigured directed link"};
        }
        const topology::Port* preceding_destination = topology_->find(previous.destination);
        const topology::Port* current_source = topology_->find(directed.source);
        if (preceding_destination == nullptr || current_source == nullptr ||
            preceding_destination->owner != current_source->owner) {
            throw std::invalid_argument{"routed chunk path is not contiguous"};
        }
        previous = directed;
    }

    if (expected_source.has_value()) {
        const topology::Port* source = topology_->find(first.source);
        if (source == nullptr || source->owner != *expected_source) {
            throw std::invalid_argument{"routed transfer source does not match path"};
        }
    }
    if (expected_destination.has_value()) {
        const topology::Port* destination = topology_->find(previous.destination);
        if (destination == nullptr || destination->owner != *expected_destination) {
            throw std::invalid_argument{"routed transfer destination does not match path"};
        }
    }
}

TransportRuntime::ChunkRecord& TransportRuntime::require_chunk(ChunkId chunk) {
    const auto iterator = chunks_.find(chunk);
    if (iterator == chunks_.end()) {
        throw std::invalid_argument{"unknown routed chunk identifier"};
    }
    return iterator->second;
}

DirectedLinkService& TransportRuntime::require_service(topology::DirectedLinkId link) {
    const auto iterator = services_.find(link);
    if (iterator == services_.end()) {
        throw std::invalid_argument{"unknown directed-link transport service"};
    }
    return iterator->second;
}

void TransportRuntime::mark_dropped_link_down(const QueueDrain& drained,
                                              topology::DirectedLinkId link) {
    const auto mark = [this, link](const TransferChunk& chunk) {
        ChunkRecord& record = require_chunk(chunk.id);
        if (record.state != ChunkTransitState::Admitted || record.hop_index != chunk.hop_index ||
            static_cast<std::size_t>(record.hop_index) >= record.route.size() ||
            record.route[record.hop_index] != link) {
            throw std::logic_error{"drained service chunk does not match routed chunk state"};
        }
        record.chunk = chunk;
        record.state = ChunkTransitState::DroppedLinkDown;
    };

    if (drained.active.has_value()) {
        mark(*drained.active);
    }
    for (const TransferChunk& chunk : drained.waiting) {
        mark(chunk);
    }
}

} // namespace nexuslab::transport
