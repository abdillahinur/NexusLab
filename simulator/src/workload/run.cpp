// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/workload/run.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/workload/dispatcher.hpp"
#include <algorithm>
#include <iomanip>
#include <ostream>
#include <stdexcept>
namespace nexuslab::workload {
namespace {
void append_waiting_jobs(TrainingReport& report, const WorkloadEngine& jobs, std::size_t count) {
    for (std::size_t id = 0; id < count; ++id) {
        const auto snapshot = jobs.snapshot(JobId{id}, report.simulation.final_time);
        if (!snapshot.has_value()) {
            throw std::logic_error{"missing training job"};
        }
        if (!terminal(snapshot->state)) {
            if (snapshot->state != JobState::Waiting) {
                throw std::logic_error{"unfinished active training job"};
            }
            report.jobs.push_back(*snapshot);
        }
    }
}
void write_loads(const TrainingReport& report, std::ostream& output) {
    for (std::size_t i = 0; i < report.load_sources.size(); ++i) {
        const auto& source = report.load_sources[i];
        output << "load_source=" << i + 1 << " kind=" << traffic::kind_name(source.kind)
               << " source_gpu=" << source.source.value()
               << " destination_gpu=" << source.destination.value()
               << " at_ns=" << source.onset.count() << " count=" << source.count
               << " interval_ns=" << source.interval.count()
               << " bytes_per_transfer=" << source.bytes.value()
               << " chunk_bytes=" << source.chunk_bytes.value() << '\n';
    }
    for (const auto& request : report.loads) {
        output << "load_source=" << request.source.value() << " request=" << request.ordinal
               << " scheduled_ns=" << request.scheduled.count()
               << " outcome=" << traffic::outcome_name(request.outcome)
               << " delivered_bytes=" << request.delivered.bytes
               << " buffer_drop_bytes=" << request.dropped_buffer_full.bytes
               << " down_drop_bytes=" << request.dropped_link_down.bytes;
        if (request.injected.has_value()) {
            output << " injected_ns=" << request.injected->count();
        }
        if (request.finished.has_value()) {
            output << " finished_ns=" << request.finished->count();
        }
        if (request.transfer.has_value()) {
            output << " transfer=" << request.transfer->value();
        }
        output << '\n';
    }
}
} // namespace
TrainingReport run_training(const TrainingScenario& scenario) {
    validate_failure_controls(scenario);
    if (scenario.controls.size() > 10000 || scenario.gpu_controls.size() > 10000) {
        throw std::length_error{"scenario control limit exceeded"};
    }
    telemetry::TelemetrySession telemetry_session{scenario.telemetry};
    const telemetry::TelemetrySink telemetry_sink = telemetry_session.sink();
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
    transport::TransportRuntime transport{*graph, links, {}, telemetry_sink};
    routing::RoutingConfiguration routing_configuration{scenario.routing_policy, scenario.seed};
    routing_configuration.capacity_metadata = scenario.failure_script.has_value();
    routing::Router router{*graph, transport, routing::PolicyRegistry{}, routing_configuration,
                           telemetry_sink};
    collective::RingExecutor collectives{*graph, router, scenario.local, telemetry_sink};
    WorkloadEngine jobs{*graph, collectives, {}, scenario.scheduling, nullptr, telemetry_sink};
    std::unique_ptr<failures::Runtime> failures;
    if (scenario.failure_script.has_value()) {
        failures = std::make_unique<failures::Runtime>(*scenario.failure_script, *graph, transport,
                                                       &collectives, &jobs, failures::Limits{},
                                                       telemetry_sink);
    }
    std::unique_ptr<traffic::Runtime> loads;
    if (scenario.failure_script.has_value()) {
        loads = std::make_unique<traffic::Runtime>(scenario.failure_script->loads, *graph, router,
                                                   traffic::Limits{}, telemetry_sink);
    }
    TrainingDispatcher dispatcher{jobs, collectives, transport, failures.get(), loads.get()};
    const sim::TraceMode trace_mode = scenario.telemetry.mode == telemetry::TelemetryMode::Full
                                          ? sim::TraceMode::Enabled
                                          : sim::TraceMode::Disabled;
    sim::Simulation simulation{scenario.seed, trace_mode, telemetry_sink};
    if (failures) {
        failures->schedule(simulation);
    }
    for (const auto& spec : scenario.jobs) {
        static_cast<void>(jobs.schedule(spec, simulation));
    }
    for (const auto& control : scenario.controls) {
        static_cast<void>(jobs.schedule_control(JobId{control.job}, control.kind, control.timestamp,
                                                simulation, control.worker));
    }
    for (const auto& control : scenario.gpu_controls) {
        static_cast<void>(
            jobs.schedule_gpu_state(control.gpu, control.healthy, control.timestamp, simulation));
    }
    if (loads) {
        loads->schedule(simulation);
    }
    const sim::SimulationResult simulation_result = simulation.run(dispatcher);
    telemetry_session.finalize(simulation_result.final_time);
    TrainingReport report{simulation_result,
                          jobs.take_completed(),
                          {},
                          {},
                          {},
                          router.take_decisions(),
                          0,
                          {},
                          telemetry_session.snapshot(),
                          std::nullopt,
                          {},
                          {}};
    if (report.simulation.status != sim::SimulationStatus::Completed) {
        throw std::runtime_error{
            report.simulation.error.value_or("training run did not finish all jobs")};
    }
    append_waiting_jobs(report, jobs, scenario.jobs.size());
    report.placements.assign(jobs.placements().begin(), jobs.placements().end());
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
    if (failures) {
        report.failures = failures->report();
        report.loads = loads->report();
        report.load_sources = scenario.failure_script->loads;
    }
    return report;
}
std::string_view state_name(JobState state) {
    switch (state) {
    case JobState::Waiting:
        return "waiting";
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
               << " waiting_ns=" << job.waiting_ns << " reason=" << job.reason << '\n';
    }
    if (report.failures.has_value()) {
        output << "failure_script_digest=" << std::hex << std::setfill('0') << std::setw(16)
               << report.failures->script_digest << std::dec << std::setfill(' ') << '\n';
        output << "domain_outcome_digest_version=" << report.failures->outcome_version << '\n';
        for (const auto& incident : report.failures->incidents) {
            output << "failure=" << incident.id.value()
                   << " kind=" << failures::kind_name(incident.specification.kind)
                   << " resource=" << failures::target_value(incident.specification.target)
                   << " applied=" << incident.applied << " recovered=" << incident.recovered
                   << " effective_operational=" << incident.effective_operational
                   << " dropped_bytes=" << incident.dropped.bytes
                   << " dropped_chunks=" << incident.dropped.chunks
                   << " affected_jobs=" << incident.affected_jobs.size()
                   << " capacity_revision=" << incident.capacity_revision << '\n';
        }
    }
    write_loads(report, output);
    if (include_timeline) {
        if (report.failures.has_value()) {
            for (const auto& entry : report.failures->timeline) {
                output << "failure_event id=" << entry.incident.value()
                       << " transition=" << failures::transition_name(entry.kind)
                       << " time_ns=" << entry.timestamp.count()
                       << " effective_operational=" << entry.effective_operational << '\n';
            }
        }
        for (const auto& entry : report.placements) {
            output << "placement job=" << entry.job.value()
                   << " time_ns=" << entry.timestamp.count() << " policy=" << entry.policy
                   << " version=" << entry.version
                   << " outcome=" << static_cast<unsigned>(entry.outcome)
                   << " requested_workers=" << entry.requested_workers
                   << " priority=" << entry.priority << " racks=" << entry.locality.racks
                   << " nics=" << entry.locality.nics
                   << " cross_rack_ring_edges=" << entry.locality.cross_rack_ring_edges
                   << " fragmentation_before=" << entry.fragmentation_before
                   << " fragmentation_after=" << entry.fragmentation_after << " workers=";
            for (const auto gpu : entry.workers) {
                output << gpu.value() << ",";
            }
            output << " reason=" << entry.reason << '\n';
        }
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
