// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/collective/model.hpp"
#include "nexuslab/routing/router.hpp"
#include <map>
namespace nexuslab::collective {
struct CollectiveConfiguration final {
    transport::BitsPerSecond local_bandwidth{800'000'000'000ULL};
    sim::SimDurationNs local_latency{0};
    std::size_t maximum_collectives{100'000};
    std::size_t maximum_participant_entries{1'000'000};
    std::size_t maximum_timeline_entries{1'000'000};
};
struct CollectiveProgress final {
    Phase phase;
    std::uint32_t round;
    std::size_t pending;
    std::uint64_t issued_fabric_bytes;
    std::uint64_t issued_local_bytes;
    std::uint64_t delivered_bytes;
    std::optional<CollectiveResult> completion;
};
class RingExecutor final : public CollectiveExecutor {
  public:
    RingExecutor(const topology::TopologyGraph& graph, routing::Router& router,
                 CollectiveConfiguration configuration = {});
    [[nodiscard]] CollectiveId submit(const CollectiveRequest& request,
                                      sim::SimulationContext& context) override;
    void cancel(CollectiveId id) override;
    void handle(const LocalCompletionEvent& event, sim::SimulationContext& context);
    void handle(const transport::TransferCompletion& completion, sim::SimulationContext& context);
    [[nodiscard]] std::vector<CollectiveResult> take_completed() override;
    [[nodiscard]] std::optional<CollectiveProgress> snapshot(CollectiveId id) const;
    [[nodiscard]] std::span<const CollectiveTimeline> timeline() const noexcept;

  private:
    struct Record final {
        CollectiveRequest request;
        CollectiveResult result;
        Phase phase{Phase::ReduceScatter};
        std::uint32_t round{0};
        std::size_t pending{0};
        bool failed{false};
        bool cancelled{false};
        bool finished{false};
    };
    struct Pending final {
        CollectiveId collective;
        std::uint64_t bytes;
    };
    struct LocalPending final {
        Pending transfer;
        LocalCompletionEvent event;
    };
    void start_round(Record& record, sim::SimulationContext& context);
    void issue(Record& record, const RingTransfer& transfer, sim::SimulationContext& context);
    void advance(Record& record, sim::SimulationContext& context);
    void finish(Record& record, sim::SimTimeNs now);
    void trace(const Record& record, sim::SimTimeNs now);
    const topology::TopologyGraph* graph_;
    routing::Router* router_;
    CollectiveConfiguration configuration_;
    std::size_t participant_entries_{0};
    std::uint64_t next_flow_{0};
    std::map<CollectiveId, Record> records_;
    std::map<transport::TransferId, Pending> transfers_;
    std::map<sim::EventId, LocalPending> locals_;
    std::vector<CollectiveResult> completed_;
    std::vector<CollectiveTimeline> timeline_;
};
} // namespace nexuslab::collective
