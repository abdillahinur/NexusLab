// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/workload/digest.hpp"

#include <string_view>
#include <type_traits>

namespace nexuslab::workload {
namespace {

class DigestBuilder final {
  public:
    void add(std::uint64_t value) noexcept {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            hash_ ^= (value >> shift) & 0xFFU;
            hash_ *= fnv_prime;
        }
    }

    void add_bool(bool value) noexcept { add(static_cast<std::uint64_t>(value)); }

    void add(std::string_view value) noexcept {
        add(static_cast<std::uint64_t>(value.size()));
        for (const char character : value) {
            hash_ ^= static_cast<unsigned char>(character);
            hash_ *= fnv_prime;
        }
    }

    template <typename Enum> void add_enum(Enum value) noexcept {
        static_assert(std::is_enum_v<Enum>);
        add(static_cast<std::uint64_t>(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

  private:
    static constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
    std::uint64_t hash_{14'695'981'039'346'656'037ULL};
};

void add_simulation(DigestBuilder& digest, const sim::SimulationResult& result) noexcept {
    digest.add_enum(result.status);
    digest.add_bool(result.stop_reason.has_value());
    if (result.stop_reason.has_value()) {
        digest.add_enum(*result.stop_reason);
    }
    digest.add(result.final_time.count());
    digest.add(result.dispatched_events);
    digest.add(result.cancelled_events);
    digest.add(static_cast<std::uint64_t>(result.pending_events));
    digest.add(result.rng_draw_count);
    digest.add_bool(result.error.has_value());
    if (result.error.has_value()) {
        digest.add(*result.error);
    }
}

void add_job(DigestBuilder& digest, const JobSnapshot& job) noexcept {
    digest.add(job.id.value());
    digest.add_enum(job.state);
    digest.add(job.completed_steps);
    digest.add(job.arrival.count());
    digest.add_bool(job.finished.has_value());
    if (job.finished.has_value()) {
        digest.add(job.finished->count());
    }
    digest.add(job.compute_gpu_ns);
    digest.add(job.idle_gpu_ns);
    digest.add(job.elapsed_ns);
    digest.add(job.reason);
    digest.add_bool(job.allocated_at.has_value());
    if (job.allocated_at.has_value()) {
        digest.add(job.allocated_at->count());
    }
    digest.add(job.waiting_ns);
}

void add_job_timeline(DigestBuilder& digest, const JobTimeline& entry) noexcept {
    digest.add(entry.job.value());
    digest.add(entry.timestamp.count());
    digest.add_enum(entry.state);
    digest.add(entry.step);
    digest.add(entry.bucket);
    digest.add(entry.action);
    digest.add_bool(entry.collective.has_value());
    if (entry.collective.has_value()) {
        digest.add(entry.collective->value());
    }
}

void add_collective(DigestBuilder& digest, const collective::CollectiveResult& result) noexcept {
    digest.add(result.id.value());
    digest.add_enum(result.outcome);
    digest.add(result.started.count());
    digest.add(result.finished.count());
    digest.add(result.planned_bytes);
    digest.add(result.issued_fabric_bytes);
    digest.add(result.issued_local_bytes);
    digest.add(result.delivered_bytes);
    digest.add(result.reason);
}

void add_collective_timeline(DigestBuilder& digest,
                             const collective::CollectiveTimeline& entry) noexcept {
    digest.add(entry.id.value());
    digest.add(entry.timestamp.count());
    digest.add_enum(entry.phase);
    digest.add(entry.round);
}

void add_node(DigestBuilder& digest, topology::NodeId node) noexcept {
    digest.add_enum(node.kind());
    digest.add(node.value());
}

void add_route(DigestBuilder& digest, const routing::RouteDecision& decision) noexcept {
    digest.add(decision.request.flow);
    add_node(digest, decision.request.endpoints.source);
    add_node(digest, decision.request.endpoints.destination);
    digest.add(decision.request.bytes.value());
    digest.add(decision.request.maximum_chunk_bytes.value());
    digest.add(decision.policy);
    digest.add(decision.version);
    digest.add(decision.timestamp.count());
    digest.add(decision.operational_revision);
    digest.add(static_cast<std::uint64_t>(decision.candidates));
    digest.add(static_cast<std::uint64_t>(decision.path.size()));
    for (const topology::DirectedLinkId link : decision.path) {
        digest.add(link.link.value());
        digest.add_enum(link.direction);
    }
    digest.add(decision.score);
    digest.add(decision.reason);
    digest.add_bool(decision.transfer.has_value());
    if (decision.transfer.has_value()) {
        digest.add(decision.transfer->value());
    }
}

void add_placement(DigestBuilder& digest, const scheduling::PlacementDecision& decision) noexcept {
    digest.add(decision.job.value());
    digest.add(decision.timestamp.count());
    digest.add(decision.policy);
    digest.add(decision.version);
    digest.add(decision.priority);
    digest.add(decision.requested_workers);
    digest.add_enum(decision.outcome);
    digest.add(static_cast<std::uint64_t>(decision.workers.size()));
    for (const topology::GpuId worker : decision.workers) {
        digest.add(worker.value());
    }
    digest.add(decision.reason);
    digest.add(static_cast<std::uint64_t>(decision.locality.racks));
    digest.add(static_cast<std::uint64_t>(decision.locality.nics));
    digest.add(static_cast<std::uint64_t>(decision.locality.cross_rack_ring_edges));
    digest.add(static_cast<std::uint64_t>(decision.fragmentation_before));
    digest.add(static_cast<std::uint64_t>(decision.fragmentation_after));
}

template <typename Range, typename Append>
void add_range(DigestBuilder& digest, const Range& values, Append append) noexcept {
    digest.add(static_cast<std::uint64_t>(values.size()));
    for (const auto& value : values) {
        append(digest, value);
    }
}

} // namespace

std::uint64_t domain_outcome_digest(const TrainingReport& report) noexcept {
    DigestBuilder digest;
    add_simulation(digest, report.simulation);
    add_range(digest, report.jobs, add_job);
    add_range(digest, report.timeline, add_job_timeline);
    add_range(digest, report.collectives, add_collective);
    add_range(digest, report.collective_timeline, add_collective_timeline);
    add_range(digest, report.decisions, add_route);
    add_range(digest, report.placements, add_placement);
    digest.add(report.maximum_waiting_bytes);
    return digest.value();
}

} // namespace nexuslab::workload
