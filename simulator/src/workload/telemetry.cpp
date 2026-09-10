// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/workload/engine.hpp"

#include "nexuslab/sim/simulation.hpp"

namespace nexuslab::workload {

void WorkloadEngine::emit_job(const Record& record, telemetry::JobTransition transition,
                              sim::SimulationContext& context, std::uint32_t worker) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation;
    correlation.event = context.current_event_id();
    correlation.cause = context.cause();
    correlation.job = record.id;
    correlation.collective = record.active_collective;
    const auto allocated =
        record.assigned ? static_cast<std::uint32_t>(record.spec.workers.size()) : 0U;
    telemetry_.record(context.now(), correlation,
                      telemetry::JobObservation{transition, record.step, record.completed_buckets,
                                                worker, allocated});
}

void WorkloadEngine::emit_terminal_metrics(const JobSnapshot& snapshot,
                                           sim::SimulationContext& context) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::Correlation correlation;
    correlation.event = context.current_event_id();
    correlation.cause = context.cause();
    correlation.job = snapshot.id;
    telemetry_.record(context.now(), correlation,
                      telemetry::HistogramObservation{
                          telemetry::MetricId::JobCompletionTimeNs, {}, snapshot.elapsed_ns});
    telemetry_.record(context.now(), correlation,
                      telemetry::HistogramObservation{
                          telemetry::MetricId::JobSchedulingWaitNs, {}, snapshot.waiting_ns});
    telemetry_.record(context.now(), correlation,
                      telemetry::CounterObservation{
                          telemetry::MetricId::JobComputeGpuNs, {}, snapshot.compute_gpu_ns});
    telemetry_.record(
        context.now(), correlation,
        telemetry::CounterObservation{telemetry::MetricId::JobGpuIdleNs, {}, snapshot.idle_gpu_ns});
    telemetry_.record(context.now(), correlation,
                      telemetry::CounterObservation{
                          telemetry::MetricId::JobCompletedSteps, {}, snapshot.completed_steps});
    telemetry_.record(context.now(), correlation,
                      telemetry::CounterObservation{telemetry::MetricId::JobTerminalTotal,
                                                    {{telemetry::MetricLabel::Outcome,
                                                      static_cast<std::uint64_t>(snapshot.state)}},
                                                    1});
}

void WorkloadEngine::emit_placement(const scheduling::PlacementDecision& decision,
                                    telemetry::PlacementDecisionId id,
                                    sim::SimulationContext& context) {
    if (!telemetry_.enabled()) {
        return;
    }
    telemetry::PlacementDecisionOutcome outcome = telemetry::PlacementDecisionOutcome::Rejected;
    if (decision.outcome == scheduling::PlacementOutcome::Placed) {
        outcome = telemetry::PlacementDecisionOutcome::Placed;
    } else if (decision.outcome == scheduling::PlacementOutcome::Waiting) {
        outcome = telemetry::PlacementDecisionOutcome::Waiting;
    }
    telemetry::Correlation correlation;
    correlation.event = context.current_event_id();
    correlation.cause = context.cause();
    correlation.job = decision.job;
    correlation.placement_decision = id;
    telemetry_.record(context.now(), correlation,
                      telemetry::PlacementDecisionObservation{
                          id, decision.policy, decision.version, decision.priority,
                          decision.requested_workers, decision.workers, decision.locality.racks,
                          decision.locality.nics, decision.locality.cross_rack_ring_edges,
                          decision.reason, outcome});
    const telemetry::MetricLabels labels{
        {telemetry::MetricLabel::Policy, telemetry::policy_label_value(decision.policy)},
        {telemetry::MetricLabel::Outcome, static_cast<std::uint64_t>(outcome)}};
    telemetry_.record(
        context.now(), correlation,
        telemetry::CounterObservation{telemetry::MetricId::PlacementDecisionTotal, labels, 1});
    if (decision.outcome == scheduling::PlacementOutcome::Placed) {
        telemetry_.record(
            context.now(), correlation,
            telemetry::HistogramObservation{telemetry::MetricId::PlacementCrossRackRingEdges,
                                            {},
                                            decision.locality.cross_rack_ring_edges});
        telemetry_.record(context.now(), correlation,
                          telemetry::HistogramObservation{telemetry::MetricId::PlacementRackCount,
                                                          {},
                                                          decision.locality.racks});
        telemetry_.record(context.now(), correlation,
                          telemetry::HistogramObservation{
                              telemetry::MetricId::PlacementNicCount, {}, decision.locality.nics});
    }
}

} // namespace nexuslab::workload
