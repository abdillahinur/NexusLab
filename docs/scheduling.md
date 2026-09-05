<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Scheduling and GPU placement

Cluster 7 adds optional whole-job admission to the synthetic training runner. Build with
`bash scripts/build.sh release`, then run:

```bash
build/release/simulator/nexuslab train --file examples/training/scheduled.yaml --timeline
bash scripts/benchmark-scheduling.sh --policy compact --case rack-pressure
bash scripts/benchmark-scheduling-suite.sh
```

The example starts a 48-GPU job, queues a higher-priority 32-GPU job, and backfills an 8-GPU
job while the larger request waits. All run the existing compute/Ring/barrier lifecycle.

## Requests and admission

Set root `scheduling_policy` to enable scheduling. A job supplies either `requested_workers`
or explicit `workers`. Pinned workers retain order and wait for those exact GPUs; dynamic
requests let the policy choose workers and rank order. A compute vector maps to that rank
order. Missing both, specifying both, invalid IDs, duplicates, and zero dimensions are errors.
A request exceeding physical GPU capacity produces a failed job with a rejected decision.
Temporarily insufficient free/healthy capacity produces a waiting job, not a failed allocation.

The waiting queue sorts descending priority, ascending arrival time, then ascending job ID.
On each arrival, release, and GPU state event, one pass tries waiting requests in that order.
A blocked request does not reserve GPUs or block smaller requests that fit. This non-reserving
backfill can starve large jobs under an unending stream of small jobs. There is no preemption.
Arrivals at the same timestamp follow stable event order; priority only reorders jobs already
waiting, not previously admitted work. Simultaneous down/up/control events have critical event
priority, ahead of normal arrivals and compute completions.

## Policies, version 1

| Name | Allocation rule |
|---|---|
| `first-fit` | Lowest free healthy GPU IDs |
| `random` | Lowest versioned hash scores of seed, job ID, GPU ID; GPU ID breaks collisions |
| `rack-local` | Lowest-ID rack able to fit the whole job; otherwise first fit |
| `compact` | Fill racks with most free GPUs first, then NICs with most free GPUs; ID ties |

Random placement uses integer mixing, not standard-library shuffle or mutable RNG state.
A golden test fixes its rank order for seed 42/job 7 on 64 free GPUs. Retries depend only on
the request, seed, and current resources. It is a deterministic pseudo-random baseline, not a
claim of uniform sampling over all allocations. Compact is greedy, not an optimal placement
solver. Locality can reduce fabric transfers but does not predict existing network congestion.

Policies receive a borrowed read-only `ClusterResourceView`. `SchedulingPolicy` can be replaced
through the WorkloadEngine constructor, with its name supplied in Configuration. Results are
validated for size, pinned order, unique valid free healthy GPUs, bounded reason, and outcome
before execution. Built-ins are selected by configuration through `make_policy`. No policy
can directly mutate clocks, jobs, routing, transport, or inventory through this interface.

## Failures, release, and unfinished jobs

`gpu_controls` adds persistent physical GPU health to scheduler inventory:

```yaml
scheduling_policy: first-fit
gpu_controls:
  - {gpu: 0, state: down, at_ns: 5000}
  - {gpu: 0, state: up, at_ns: 20000}
```

A down GPU cannot be allocated. If allocated, its owner fails and releases its entire allocation;
the GPU remains failed until up. Recovery triggers admission. Each control references an existing
GPU; at most 10,000 GPU controls are accepted. This does not change topology serialization or
infer GPU failure from a fabric link failure. Job `worker_failure` retains its job-scoped meaning.

Completion, cancellation, and failure release resources exactly once from the workload lifecycle.
Waiting cancellation releases no other job's allocation. Issued network/local traffic may drain
after release and contend with a new job; no rollback or retry is implied. Persistent capacity
shortage can leave jobs waiting when no events remain. Reports retain `state=waiting`, no finish
or allocation time, and wait measured to the final observation; CLI exits 1. Queue exhaustion is
not automatically job success. Future recovery must be scheduled before the run.

## Metrics and records

- Job elapsed includes scheduling wait. `waiting_ns` ends at allocation, cancellation/failure,
  or observation if still waiting. Pre-arrival cancellation has zero wait.
- `idle_gpu_ns` is allocated-worker time minus compute, starting at allocation; it excludes queue wait.
- Each placement attempt records job/time, policy/version, requested size, priority, outcome/reason,
  assigned GPU IDs, rack/NIC counts, cross-rack ring edges, and fragmentation before/after.
- Fragmentation is free healthy GPUs outside the largest free rack. It measures dispersed free
  capacity; those GPUs can still serve jobs spanning racks. Failed/allocated GPUs are excluded.
- Ring-edge locality follows assigned rank order, including the closing edge. One GPU has zero
  cross-rack edges. Rank order can affect paths independently of the set of GPUs selected.

`--timeline` prints decisions (`outcome=1` placed, `2` waiting, `3` rejected), allocation events,
and existing job/collective timelines. C++ reports expose structured records for future visualization.
Rack/NIC counts are not modeled bandwidth savings: local communication currently has independent
transfer timing and no shared-bus contention. Placement comparisons inherit that synthetic limitation.

Inventory is bounded at 8,192 GPUs. Workload job/worker/event/timeline limits still apply. Scheduler
Configuration separately bounds retained decisions (default one million) and cumulative allocation
IDs (one million). Waiting queue scans and sorting are bounded by job count; decisions may repeat
when a new arrival/state change retries blocked jobs. There is no polling event loop. Exhausted
limits fail the run explicitly rather than silently dropping records or resuming partial state.

[ADR-010](adr/ADR-010-scheduler-placement-boundary.md) records the accepted boundary and deferrals.
[Gate 7](architecture-gates/cluster-7.md) and the [comparison report](benchmarks/cluster-7-scheduling-baseline.md)
record validation. Reservations, preemption, GPU memory capacity, predicted network costs and
production scheduling protocols remain outside this cluster.
