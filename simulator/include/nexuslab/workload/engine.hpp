// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/collective/model.hpp"
#include "nexuslab/sim/event_id.hpp"
#include "nexuslab/topology/graph.hpp"
#include "nexuslab/workload/model.hpp"
#include <map>
#include <span>
#include <utility>
namespace nexuslab::sim {
class Simulation;
}
namespace nexuslab::workload {
class WorkloadEngine final {
  public:
    WorkloadEngine(const topology::TopologyGraph& graph, collective::CollectiveExecutor& executor,
                   WorkloadLimits limits = {});
    [[nodiscard]] JobId schedule(JobSpec specification, sim::Simulation& simulation);
    [[nodiscard]] sim::EventId schedule_control(JobId job, WorkloadEventKind kind,
                                                sim::SimTimeNs when, sim::Simulation& simulation,
                                                std::uint32_t worker = 0) const;
    void handle(const WorkloadEvent& event, sim::SimulationContext& context);
    void handle(const collective::CollectiveResult& completion, sim::SimulationContext& context);
    [[nodiscard]] std::optional<JobSnapshot> snapshot(JobId id, sim::SimTimeNs now) const;
    [[nodiscard]] std::vector<JobSnapshot> take_completed();
    [[nodiscard]] std::span<const JobTimeline> timeline() const noexcept;

  private:
    struct Record final {
        Record(JobId job, JobSpec specification) : id{job}, spec{std::move(specification)} {}
        JobId id;
        JobSpec spec;
        JobState state{JobState::Scheduled};
        std::uint32_t step{0};
        std::uint32_t buckets{0};
        std::uint32_t completed_buckets{0};
        std::uint32_t compute_complete{0};
        sim::SimTimeNs step_started;
        std::uint64_t compute_gpu_ns{0};
        std::optional<sim::SimTimeNs> finished;
        std::optional<sim::EventId> arrival_event;
        std::optional<collective::CollectiveId> active_collective;
        std::vector<std::uint32_t> ready;
        std::vector<std::optional<sim::EventId>> events;
        std::string reason;
        bool assigned{false};
    };
    [[nodiscard]] std::uint32_t validate(const JobSpec& spec) const;
    void arrive(Record& record, sim::SimulationContext& context);
    void start_step(Record& record, sim::SimulationContext& context);
    void compute_ready(Record& record, const WorkloadEvent& event, sim::SimulationContext& context);
    void start_collective(Record& record, sim::SimulationContext& context);
    void finish(Record& record, JobState state, std::string reason,
                sim::SimulationContext& context);
    void trace(const Record& record, sim::SimTimeNs now, std::string action);
    [[nodiscard]] static JobSnapshot inspect(const Record& record, sim::SimTimeNs now);
    const topology::TopologyGraph* graph_;
    collective::CollectiveExecutor* executor_;
    WorkloadLimits limits_;
    std::size_t worker_entries_{0};
    std::size_t event_entries_{0};
    std::map<JobId, Record> jobs_;
    std::map<topology::GpuId, JobId> assignments_;
    std::map<collective::CollectiveId, JobId> collectives_;
    std::vector<JobSnapshot> completed_;
    std::vector<JobTimeline> timeline_;
};
} // namespace nexuslab::workload
