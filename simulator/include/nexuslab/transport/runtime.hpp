// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/sim/time.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/transport/events.hpp"
#include "nexuslab/transport/link_service.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace nexuslab::sim {
class SimulationContext;
}

namespace nexuslab::transport {

struct RoutedChunk final {
    TransferChunk chunk;
    std::vector<topology::DirectedLinkId> route;

    bool operator==(const RoutedChunk&) const = default;
};

struct TransferRequest final {
    topology::NodeId source;
    topology::NodeId destination;
    ByteCount bytes;
    ByteCount maximum_chunk_bytes;
    std::vector<topology::DirectedLinkId> route;

    bool operator==(const TransferRequest&) const = default;
};

struct SubmittedChunk final {
    ChunkId id;
    ByteCount bytes;

    bool operator==(const SubmittedChunk&) const = default;
};

struct SubmittedTransfer final {
    TransferId id;
    std::vector<SubmittedChunk> chunks;

    bool operator==(const SubmittedTransfer&) const = default;
};

enum class ChunkTransitState : std::uint8_t {
    Registered = 1,
    AwaitingArrival = 2,
    Admitted = 3,
    Delivered = 4,
    DroppedBufferFull = 5,
    DroppedLinkDown = 6,
};

struct ChunkTransitSnapshot final {
    ChunkTransitState state;
    std::uint32_t hop_index;
    bool marked;

    bool operator==(const ChunkTransitSnapshot&) const = default;
};

enum class TransferOutcome : std::uint8_t { Pending = 1, Succeeded = 2, Failed = 3 };

struct TransferCompletion final {
    TransferId transfer;
    TransferOutcome outcome;
    sim::SimTimeNs timestamp;
    TrafficCount delivered;
    TrafficCount dropped_buffer_full;
    TrafficCount dropped_link_down;
    bool operator==(const TransferCompletion&) const = default;
};

struct TransferSnapshot final {
    TrafficCount total;
    TrafficCount registered;
    TrafficCount awaiting_initial_arrival;
    TrafficCount waiting;
    TrafficCount active;
    TrafficCount propagating;
    TrafficCount delivered;
    TrafficCount dropped_buffer_full;
    TrafficCount dropped_link_down;
    std::optional<sim::SimTimeNs> started_at;
    std::optional<TransferCompletion> completion;
    bool operator==(const TransferSnapshot&) const = default;
};

struct TransportLimits final {
    std::size_t maximum_chunks{1'000'000};
    std::size_t maximum_route_entries{16'000'000};
    std::size_t maximum_route_hops{1'024};
};

class TransportRuntime final {
  public:
    TransportRuntime(topology::TopologyGraph& topology,
                     const std::vector<DirectedLinkConfiguration>& configurations,
                     TransportLimits limits = {});

    void register_chunk(RoutedChunk routed_chunk);
    [[nodiscard]] SubmittedTransfer submit_transfer(const TransferRequest& request,
                                                    sim::SimulationContext& context);
    [[nodiscard]] sim::EventId schedule_initial_arrival(ChunkId chunk,
                                                        sim::SimulationContext& context);
    [[nodiscard]] sim::EventId schedule_link_state_change(topology::LinkId link,
                                                          topology::OperationalState state,
                                                          sim::SimTimeNs timestamp,
                                                          sim::SimulationContext& context);

    [[nodiscard]] sim::EventId schedule_port_state_change(topology::PortId port,
                                                          topology::OperationalState state,
                                                          sim::SimTimeNs timestamp,
                                                          sim::SimulationContext& context);
    [[nodiscard]] sim::EventId schedule_switch_state_change(topology::SwitchId network_switch,
                                                            topology::OperationalState state,
                                                            sim::SimTimeNs timestamp,
                                                            sim::SimulationContext& context);
    void handle_port_state_change(const PortStateChangeEvent& event,
                                  sim::SimulationContext& context);
    void handle_switch_state_change(const SwitchStateChangeEvent& event,
                                    sim::SimulationContext& context);
    [[nodiscard]] std::optional<TransferSnapshot> transfer_snapshot(TransferId id) const;
    [[nodiscard]] std::vector<TransferCompletion> take_completed_transfers();

    void handle_arrival(const ChunkArrivalEvent& event, sim::SimulationContext& context);
    void handle_completion(const TransmissionCompleteEvent& event, sim::SimulationContext& context);
    void handle_link_state_change(const LinkStateChangeEvent& event,
                                  sim::SimulationContext& context);

    [[nodiscard]] std::optional<ChunkTransitSnapshot> chunk_snapshot(ChunkId chunk) const noexcept;
    [[nodiscard]] const DirectedLinkService*
    find_service(topology::DirectedLinkId link) const noexcept;

  private:
    struct ChunkRecord final {
        TransferChunk chunk;
        std::vector<topology::DirectedLinkId> route;
        ChunkTransitState state;
        std::uint32_t hop_index;
        std::optional<sim::EventId> scheduled_arrival;
    };

    struct TransferRecord final {
        std::vector<ChunkId> chunks;
        TrafficCount total;
        TrafficCount delivered;
        TrafficCount dropped_buffer_full;
        TrafficCount dropped_link_down;
        std::optional<sim::SimTimeNs> started_at;
        std::optional<TransferCompletion> completion;
    };
    void record_terminal(const ChunkRecord& record, sim::SimTimeNs now);
    void reconcile_unavailable(sim::SimulationContext& context);
    void validate_capacity(std::size_t count, std::size_t hops) const;
    void validate_timing(ByteCount bytes, std::span<const topology::DirectedLinkId> route,
                         sim::SimTimeNs now) const;

    [[nodiscard]] topology::DirectedLink require_fabric_arc(topology::DirectedLinkId link) const;
    void validate_route(std::span<const topology::DirectedLinkId> route,
                        std::optional<topology::NodeId> expected_source,
                        std::optional<topology::NodeId> expected_destination) const;
    [[nodiscard]] ChunkRecord& require_chunk(ChunkId chunk);
    [[nodiscard]] DirectedLinkService& require_service(topology::DirectedLinkId link);

    void mark_dropped_link_down(const QueueDrain& drained, topology::DirectedLinkId link,
                                sim::SimTimeNs now);

    topology::TopologyGraph* topology_;
    TransportLimits limits_;
    std::size_t route_entries_{0};
    std::map<TransferId, TransferRecord> transfers_;
    std::vector<TransferCompletion> completions_;
    SequentialTransportIdGenerator<TransferId> transfer_ids_;
    SequentialTransportIdGenerator<ChunkId> chunk_ids_;
    std::map<topology::DirectedLinkId, DirectedLinkService> services_;
    std::map<ChunkId, ChunkRecord> chunks_;
};

} // namespace nexuslab::transport
