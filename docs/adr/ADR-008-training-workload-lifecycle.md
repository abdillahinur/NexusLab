<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-008 — Synthetic training workload lifecycle

Date: 2026-09-05. Status: Accepted; validated by Architecture Gate 5.

Jobs have explicit GPU assignments, arrival time, step count, per-worker compute duration,
gradient bytes, bucket bytes, transfer chunk bytes, priority metadata, AllReduce collective type,
and executor algorithm. Assignments are supplied by configuration, not a hidden scheduling policy.
Workers must exist and be unique. Concurrent jobs may use disjoint GPUs; an assignment conflict at
arrival fails explicitly. Automatic placement and queuing remain Cluster 7.

Each step performs compute, ordered gradient-bucket AllReduces, then a barrier. Without overlap,
buckets become ready after all workers finish compute. With overlap, worker bucket readiness is
proportional to its compute duration using checked integer arithmetic; a bucket starts only after
all workers reach that boundary and the preceding bucket collective completes. Per-worker duration
models stragglers. Computation is continuous and does not slow down when network communication
overlaps; reduction arithmetic and memory contention are not calibrated hardware models.

Job states distinguish scheduled, computing, overlapping, communicating, succeeded, failed and
cancelled. Typed workload events carry job/worker/step/bucket identity and have explicit trace
encoding. Compute events are validated against scheduled IDs and current step state. Job-scoped worker failure
and job cancellation use critical priority, cancel future compute, stop future collective rounds,
and release assignments. Already submitted transfers drain under existing no-retry semantics.
A terminal job never resumes. GPU reuse waits for no compute (cancelled synchronously); orphaned
network traffic may still consume bandwidth and is explicitly counted by the collective executor.

Job completion time runs from arrival to terminal outcome. Allocated GPU time is elapsed time times
workers. Compute time sums actual elapsed compute intervals per worker, including partial intervals
on cancellation/failure. Idle time is allocated minus compute time and includes communication/barrier
waiting. Overlapped intervals are not double-counted. Snapshots include partial active jobs and a
drain-once outcome stream. A bounded timeline records state/step/bucket changes for inspection.

WorkloadEngine depends on an abstract CollectiveExecutor and consumes explicit completion results;
collective planning, routing, and transport remain separate. A composition dispatcher exclusively
owns transport completion consumption and forwards results to the executor then the workload engine.
It never silently consumes unrelated completions. Failure results stop the job; kernel queue
exhaustion alone is not interpreted as job success.

Positive limits bound jobs, workers/job, steps/job, buckets/step, scheduled compute events/job and
retained timeline entries before large allocations. Global worker and compute-event slots are
also bounded, and YAML scalar/name lengths and cumulative worker references limit alias expansion. Time/byte arithmetic is checked. Allocation or
kernel event-ID exhaustion remains fatal without rollback. Synthetic named profiles and a strict
versioned YAML scenario use the already-pinned yaml-cpp dependency; unknown keys and unsupported
collectives fail explicitly. Profiles label all assumptions as approximations.
