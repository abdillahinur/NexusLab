<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-010: Bounded admission scheduling and GPU placement

Date: 2026-09-05. Status: Accepted; validated by Architecture Gate 7.

The optional scheduler owns a GPU inventory and chooses whole-job, non-preemptive allocations.
A read-only resource span feeds a replaceable SchedulingPolicy; policies cannot change routing,
transport, clocks, inventory, or jobs. Inventory validates a complete allocation before mutation.
Each GPU is healthy/free, healthy/allocated, or failed; GPU down/up controls persist until recovery.
Down aborts its owner and releases all that job's GPUs while retaining the failed GPU's health.
Already-issued communication still drains. Job-scoped worker_failure retains its old semantics.

Jobs specify either ordered pinned workers or requested_workers, never both. Scheduling is opt-in
through scheduling_policy; without it the explicit-assignment behavior remains unchanged. Compute
vectors correspond to assigned rank order. Requests larger than total physical capacity fail;
requests temporarily blocked by allocations or failures wait. Pinned requests also wait in scheduler
mode. Arrival and resource release/recovery trigger one admission pass. Waiting jobs sort by descending
priority, arrival timestamp, then stable job ID. A blocked job does not prevent a smaller job starting:
this is non-reserving backfill, with no starvation guarantee. Equal-time arrivals are processed in
stable event order, not batched; priorities do not preempt a job already admitted at that timestamp.

First fit chooses ascending GPU ID. Random placement sorts free GPUs by a versioned explicit integer
hash of seed, job ID, and GPU ID (GPU ID breaks hash ties), independent of mutable RNG state.
Rack-local tries the smallest-ID rack that fits, otherwise first fit. Compact orders racks by free
capacity descending, then NICs within each rack by free capacity descending; ties use IDs. It fills
those groups greedily, favoring fewer rack/NIC boundaries but is not an optimal graph partitioner.
Pinned placements retain caller rank order. No policy reads traffic or predicts network cost.

Decisions record time, job, policy/version, priority, requested size, outcome/reason, GPU IDs,
racks/NICs used, cross-rack ring edges, and fragmentation before/after. Fragmentation is free healthy
GPUs outside the largest free rack; it is a locality measure, not unusable aggregate capacity.
Wait time is arrival to allocation (or observation/terminal time if never allocated). GPU idle counts
only time after allocation. Job elapsed includes queueing. At event-queue exhaustion, waiting jobs
remain explicit unfinished results, never fabricated successes or failures. The CLI returns nonzero.

Inventory is limited to 8,192 GPUs, jobs to existing workload bounds; decision entries and cumulative
recorded allocation IDs are separately bounded. Resource views are borrowed, with no graph copies.
Allocation decisions and compute events consume checked bounded storage. Limit exhaustion is fatal,
not rollback/resume. No new dependency, reservation, preemption, gang co-scheduling, GPU-memory,
power, persistent checkpoint, or production scheduler behavior is included in Cluster 7.
