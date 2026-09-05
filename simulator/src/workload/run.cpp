// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/workload/run.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/workload/dispatcher.hpp"
#include <algorithm>
#include <ostream>
#include <stdexcept>
namespace nexuslab::workload {
TrainingReport run_training(const TrainingScenario& scenario) {
    auto graph = topology::generate_clos({scenario.gpus, 8, 8, 8});
    std::vector<transport::DirectedLinkConfiguration> links;
    for (const auto& link : graph->links()) {
        if (link.kind == topology::LinkKind::Fabric) {
            for (const auto& arc : topology::directed_links(link)) {
                links.push_back({arc.id, scenario.bandwidth, scenario.propagation, scenario.buffer,
                                 std::nullopt});
            }
        }
    }
    transport::TransportRuntime transport{*graph, links};
    routing::Router router{
        *graph, transport, routing::PolicyRegistry{}, {scenario.routing_policy, scenario.seed}};
    collective::RingExecutor collectives{*graph, router, scenario.local};
    WorkloadEngine jobs{*graph, collectives};
    TrainingDispatcher dispatcher{jobs, collectives, transport};
    sim::Simulation simulation{scenario.seed, sim::TraceMode::Disabled};
    for (const auto& spec : scenario.jobs) {
        static_cast<void>(jobs.schedule(spec, simulation));
    }
    for (const auto& control : scenario.controls) {
        static_cast<void>(jobs.schedule_control(JobId{control.job}, control.kind, control.timestamp,
                                                simulation, control.worker));
    }
    TrainingReport report{simulation.run(dispatcher), jobs.take_completed(), {}, {}, {},
                          router.take_decisions()};
    if (report.simulation.status != sim::SimulationStatus::Completed ||
        report.jobs.size() != scenario.jobs.size()) {
        throw std::runtime_error{
            report.simulation.error.value_or("training run did not finish all jobs")};
    }
    report.timeline.assign(jobs.timeline().begin(), jobs.timeline().end());
    report.collective_timeline.assign(collectives.timeline().begin(), collectives.timeline().end());
    for (std::uint64_t id = 0;; ++id) {
        const auto snapshot = collectives.snapshot(collective::CollectiveId{id});
        if (!snapshot.has_value()) {
            break;
        }
        if (!snapshot->completion.has_value()) {
            throw std::logic_error{"collective remained pending at queue exhaustion"};
        }
        report.collectives.push_back(*snapshot->completion);
    }
    for (const auto& link : links) {
        report.maximum_waiting_bytes = std::max(
            report.maximum_waiting_bytes,
            transport.find_service(link.link)->queue().snapshot().maximum_waiting_bytes.value());
    }
    return report;
}
std::string_view state_name(JobState state) {
    switch (state) {
    case JobState::Scheduled:
        return "scheduled";
    case JobState::Computing:
        return "computing";
    case JobState::Communicating:
        return "communicating";
    case JobState::Overlapping:
        return "overlapping";
    case JobState::Succeeded:
        return "succeeded";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    throw std::invalid_argument{"unknown job state"};
}
void write_report(const TrainingReport& report, std::ostream& output, bool include_timeline) {
    output << "synthetic=true\nmodel=training-ring-v1\njobs=" << report.jobs.size()
           << "\nevents=" << report.simulation.dispatched_events
           << "\nfinal_time_ns=" << report.simulation.final_time.count()
           << "\ncollectives=" << report.collectives.size()
           << "\nroute_decisions=" << report.decisions.size()
           << "\nmaximum_waiting_bytes=" << report.maximum_waiting_bytes << '\n';
    for (const auto& job : report.jobs) {
        output << "job=" << job.id.value() << " state=" << state_name(job.state)
               << " steps=" << job.completed_steps << " elapsed_ns=" << job.elapsed_ns
               << " compute_gpu_ns=" << job.compute_gpu_ns << " idle_gpu_ns=" << job.idle_gpu_ns
               << " reason=" << job.reason << '\n';
    }
    if (include_timeline) {
        for (const auto& entry : report.timeline) {
            output << "job_event job=" << entry.job.value()
                   << " time_ns=" << entry.timestamp.count() << " step=" << entry.step
                   << " bucket=" << entry.bucket << " state=" << state_name(entry.state)
                   << " action=" << entry.action;
            if (entry.collective.has_value()) {
                output << " collective=" << entry.collective->value();
            }
            output << '\n';
        }
        for (const auto& entry : report.collective_timeline) {
            output << "collective_event id=" << entry.id.value()
                   << " time_ns=" << entry.timestamp.count()
                   << " phase=" << collective::phase_name(entry.phase) << " round=" << entry.round
                   << '\n';
        }
    }
}
} // namespace nexuslab::workload
