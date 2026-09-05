// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/workload/scenario.hpp"
#include <iosfwd>
namespace nexuslab::workload {
struct TrainingReport final {
    sim::SimulationResult simulation;
    std::vector<JobSnapshot> jobs;
    std::vector<JobTimeline> timeline;
    std::vector<collective::CollectiveResult> collectives;
    std::vector<collective::CollectiveTimeline> collective_timeline;
    std::vector<routing::RouteDecision> decisions;
    std::uint64_t maximum_waiting_bytes{0};
};
[[nodiscard]] TrainingReport run_training(const TrainingScenario& scenario);
void write_report(const TrainingReport& report, std::ostream& output, bool include_timeline);
[[nodiscard]] std::string_view state_name(JobState state);
} // namespace nexuslab::workload
