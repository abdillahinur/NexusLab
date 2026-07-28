// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/time.hpp"
#include "nexuslab/topology/entities.hpp"
#include "nexuslab/transport/types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace nexuslab::transport {

struct DirectedLinkConfiguration final {
    topology::DirectedLinkId link;
    BitsPerSecond bandwidth;
    sim::SimDurationNs propagation_delay;
    ByteCount waiting_buffer_capacity;
    std::optional<ByteCount> marking_threshold;

    bool operator==(const DirectedLinkConfiguration&) const = default;
};

struct TransferChunk final {
    TransferId transfer;
    ChunkId id;
    ByteCount bytes;
    std::uint32_t hop_index;
    bool marked{false};

    bool operator==(const TransferChunk&) const = default;
};

enum class AdmissionDisposition : std::uint8_t {
    ServiceStarted = 1,
    Queued = 2,
    DroppedBufferFull = 3,
};

struct AdmissionResult final {
    AdmissionDisposition disposition;
    bool marked_here;

    bool operator==(const AdmissionResult&) const = default;
};

struct ServiceTransition final {
    TransferChunk completed;
    std::optional<TransferChunk> next_started;

    bool operator==(const ServiceTransition&) const = default;
};

struct QueueSnapshot final {
    ByteCount waiting_bytes;
    std::size_t waiting_chunks;
    ByteCount maximum_waiting_bytes;
    std::size_t maximum_waiting_chunks;
    bool busy;

    bool operator==(const QueueSnapshot&) const = default;
};

struct QueueDrain final {
    std::optional<TransferChunk> active;
    std::vector<TransferChunk> waiting;

    bool operator==(const QueueDrain&) const = default;
};

class DirectedLinkQueue final {
  public:
    explicit DirectedLinkQueue(DirectedLinkConfiguration configuration);

    [[nodiscard]] const DirectedLinkConfiguration& configuration() const noexcept;
    [[nodiscard]] const TransferChunk* active() const noexcept;
    [[nodiscard]] QueueSnapshot snapshot() const noexcept;

    [[nodiscard]] AdmissionResult admit(TransferChunk chunk);
    [[nodiscard]] std::optional<ServiceTransition> complete_service();
    [[nodiscard]] QueueDrain drain();

  private:
    DirectedLinkConfiguration configuration_;
    std::optional<TransferChunk> active_;
    std::deque<TransferChunk> waiting_;
    std::uint64_t waiting_bytes_{0};
    std::uint64_t maximum_waiting_bytes_{0};
    std::size_t maximum_waiting_chunks_{0};
};

} // namespace nexuslab::transport
