// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/transport/timing.hpp"
#include <set>
#include <stdexcept>
#include <utility>
namespace nexuslab::collective {
RingExecutor::RingExecutor(const topology::TopologyGraph& graph, routing::Router& router,
                           CollectiveConfiguration configuration)
    : graph_{&graph}, router_{&router}, configuration_{configuration} {
    if (configuration.local_bandwidth.value() == 0 || configuration.maximum_collectives == 0 ||
        configuration.maximum_participant_entries == 0 ||
        configuration.maximum_timeline_entries == 0) {
        throw std::invalid_argument{"invalid collective configuration"};
    }
}
CollectiveId RingExecutor::submit(const CollectiveRequest& request,
                                  sim::SimulationContext& context) {
    if (request.participants.empty() || request.participants.size() > 8192 ||
        request.chunk_bytes.value() == 0) {
        throw std::invalid_argument{"invalid collective participants or chunk size"};
    }
    const auto volume =
        planned_volume(static_cast<std::uint32_t>(request.participants.size()), request.bytes);
    if (records_.size() >= configuration_.maximum_collectives ||
        request.participants.size() >
            configuration_.maximum_participant_entries - participant_entries_) {
        throw std::length_error{"collective retained-state limit exceeded"};
    }
    std::set<topology::GpuId> unique;
    for (const auto gpu : request.participants) {
        if (graph_->find(gpu) == nullptr || !unique.insert(gpu).second) {
            throw std::invalid_argument{"unknown or duplicate collective GPU"};
        }
    }
    const CollectiveId id{records_.size()};
    Record record{request,
                  {id, Phase::ReduceScatter, context.now(), context.now(), volume, 0, 0, 0, {}}};
    auto& stored = records_.emplace(id, std::move(record)).first->second;
    participant_entries_ += request.participants.size();
    if (request.participants.size() == 1) {
        finish(stored, context.now());
    } else {
        start_round(stored, context);
    }
    return id;
}
void RingExecutor::trace(const Record& record, sim::SimTimeNs now) {
    if (timeline_.size() == configuration_.maximum_timeline_entries) {
        throw std::length_error{"collective timeline limit exceeded"};
    }
    timeline_.push_back({record.result.id, now, record.phase, record.round});
}
void RingExecutor::start_round(Record& record, sim::SimulationContext& context) {
    trace(record, context.now());
    const auto plan = plan_round(static_cast<std::uint32_t>(record.request.participants.size()),
                                 record.request.bytes, record.phase, record.round);
    for (const auto& transfer : plan) {
        if (transfer.bytes.value() != 0 && !record.failed) {
            issue(record, transfer, context);
        }
    }
    if (record.pending == 0) {
        finish(record, context.now());
    }
}
void RingExecutor::issue(Record& record, const RingTransfer& transfer,
                         sim::SimulationContext& context) {
    const auto source = graph_->find(record.request.participants[transfer.source])->attached_nic;
    const auto destination =
        graph_->find(record.request.participants[transfer.destination])->attached_nic;
    if (source == destination) {
        const auto duration =
            transport::serialization_delay(transfer.bytes, configuration_.local_bandwidth);
        if (!duration.has_value()) {
            throw std::overflow_error{"local collective delay overflow"};
        }
        const auto end = sim::checked_add(context.now(), *duration);
        const auto arrival =
            end.has_value() ? sim::checked_add(*end, configuration_.local_latency) : std::nullopt;
        if (!arrival.has_value()) {
            throw std::overflow_error{"local collective arrival overflow"};
        }
        const auto event = context.schedule(
            {*arrival, sim::EventPriority::Control,
             LocalCompletionEvent{record.result.id, record.round, transfer.source, record.phase}});
        locals_.emplace(
            event, LocalPending{{record.result.id, transfer.bytes.value()},
                                {record.result.id, record.round, transfer.source, record.phase}});
        record.result.issued_local_bytes += transfer.bytes.value();
    } else {
        if (next_flow_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error{"collective routing key exhausted"};
        }
        const auto submitted =
            router_->submit({next_flow_++,
                             {topology::NodeId{source}, topology::NodeId{destination}},
                             transfer.bytes,
                             record.request.chunk_bytes},
                            context);
        if (!submitted.has_value()) {
            record.failed = true;
            record.result.reason = "no route for collective round";
            return;
        }
        transfers_.emplace(submitted->id, Pending{record.result.id, transfer.bytes.value()});
        record.result.issued_fabric_bytes += transfer.bytes.value();
    }
    ++record.pending;
}
void RingExecutor::handle(const LocalCompletionEvent& event, sim::SimulationContext& context) {
    const auto found = locals_.find(context.current_event_id());
    if (found == locals_.end() || found->second.event != event) {
        throw std::logic_error{"unexpected local collective event"};
    }
    auto& record = records_.at(event.collective);
    if (record.round != event.round || record.phase != event.phase ||
        event.rank >= record.request.participants.size()) {
        throw std::logic_error{"stale local collective event"};
    }
    record.result.delivered_bytes += found->second.transfer.bytes;
    locals_.erase(found);
    --record.pending;
    advance(record, context);
}
void RingExecutor::handle(const transport::TransferCompletion& completion,
                          sim::SimulationContext& context) {
    const auto found = transfers_.find(completion.transfer);
    if (found == transfers_.end()) {
        throw std::logic_error{"collective executor received unowned transfer outcome"};
    }
    const auto accounted = transport::add_traffic(
        completion.delivered,
        transport::add_traffic(completion.dropped_buffer_full, completion.dropped_link_down));
    if (accounted.bytes != found->second.bytes) {
        throw std::logic_error{"collective transfer byte mismatch"};
    }
    auto& record = records_.at(found->second.collective);
    record.result.delivered_bytes += completion.delivered.bytes;
    if (completion.outcome != transport::TransferOutcome::Succeeded) {
        record.failed = true;
        record.result.reason = "collective fabric transfer failed";
    }
    transfers_.erase(found);
    --record.pending;
    advance(record, context);
}
void RingExecutor::advance(Record& record, sim::SimulationContext& context) {
    if (record.pending != 0) {
        return;
    }
    if (record.failed || record.cancelled) {
        finish(record, context.now());
        return;
    }
    if (++record.round == record.request.participants.size() - 1) {
        if (record.phase == Phase::AllGather) {
            finish(record, context.now());
            return;
        }
        record.phase = Phase::AllGather;
        record.round = 0;
    }
    start_round(record, context);
}
void RingExecutor::finish(Record& record, sim::SimTimeNs now) {
    if (record.finished) {
        throw std::logic_error{"collective completed twice"};
    }
    record.finished = true;
    record.phase = Phase::Succeeded;
    if (record.failed) {
        record.phase = Phase::Failed;
    }
    if (record.cancelled) {
        record.phase = Phase::Cancelled;
    }
    if (record.phase == Phase::Succeeded &&
        record.result.delivered_bytes != record.result.planned_bytes) {
        throw std::logic_error{"ring byte conservation failure"};
    }
    record.result.outcome = record.phase;
    record.result.finished = now;
    trace(record, now);
    completed_.push_back(record.result);
}
void RingExecutor::cancel(CollectiveId id) {
    auto& record = records_.at(id);
    if (!record.finished) {
        record.cancelled = true;
        record.result.reason = "collective cancelled";
    }
}
std::vector<CollectiveResult> RingExecutor::take_completed() {
    return std::exchange(completed_, {});
}
std::span<const CollectiveTimeline> RingExecutor::timeline() const noexcept { return timeline_; }
std::optional<CollectiveProgress> RingExecutor::snapshot(CollectiveId id) const {
    const auto found = records_.find(id);
    if (found == records_.end()) {
        return std::nullopt;
    }
    const auto& r = found->second;
    return CollectiveProgress{r.phase,
                              r.round,
                              r.pending,
                              r.result.issued_fabric_bytes,
                              r.result.issued_local_bytes,
                              r.result.delivered_bytes,
                              r.finished ? std::optional{r.result} : std::nullopt};
}
} // namespace nexuslab::collective
