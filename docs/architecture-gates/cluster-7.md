<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 7

Date: 2026-09-05. Scope: Scheduler and GPU Placement / Milestone 5 Multi-Tenant Cluster.
Decision: approved. **Proceed: YES.**

| Requirement | Evidence | Result |
|---|---|---|
| Resource inventory and atomic allocation | Known unique free/healthy GPU validation before mutation; exact-fit, duplicate, unknown, conflicting-owner tests | Pass |
| Replaceable read-only policy boundary | SchedulingPolicy, borrowed ClusterResourceView, constructor injection; invalid custom result rejected | Pass |
| Four deterministic placement policies | First fit, seeded random golden rank order, rack-local and compact rack/NIC capacity tests | Pass |
| Waiting queue and priorities | Priority → arrival → job ID tests; event-order admission and non-reserving backfill documented | Pass |
| Lifecycle and GPU release | Full dynamic allocation reuse, queued/active cancellation, partial metrics and communication drain tests | Pass |
| Failed GPU exclusion and recovery | Persistent down/up, owner abort, waiting until recovery, explicit permanent-capacity shortage | Pass |
| Separate scheduling wait and GPU idle | 900-ns queue delay + 1,000-ns compute gives 1,900-ns elapsed, zero GPU idle | Pass |
| Locality and fragmentation | Rack/NIC counts, closing ring edge, free capacity outside largest rack; fragmented rack/NIC tests | Pass |
| Placement affects communication | Compact same-NIC placement produces no fabric routes; random placement does; end-to-end matrix | Pass |
| Explainable bounded decisions | Job/time, policy/version, priority, requested/assigned IDs, outcome/reason, locality and before/after fragmentation | Pass |
| Repeatability and failure reporting | All 108 policy processes repeat domain fields; six kernel controls; unexpected failures abort | Pass |

## Correctness and quality

**224/224 CTest checks pass** under GCC 13.3, Clang 18.1.3 with clang-tidy, and Clang
AddressSanitizer/UndefinedBehaviorSanitizer. Both compilers use warnings as errors. Existing
198 checks remain intact; 24 scheduler tests plus scenario and benchmark smoke tests were added.
No dependencies or warning suppressions were added. Formatting and `git diff --check` pass.
The workflow includes a scheduler benchmark smoke run. Hosted CI has not run for these changes.

Validation uses `cmake --build DIRECTORY` followed by
`ctest --test-dir DIRECTORY --output-on-failure` for `/tmp/build-gcc`, `/tmp/build-lint` and
`/tmp/build-sanitize` in the Linux container. `/tmp/build-release` supplies benchmark binaries.
Fresh builds can use existing dev/lint/sanitize/release presets with the appropriate compilers.
`bash scripts/format.sh --check` verifies formatting.

The [benchmark report](../benchmarks/cluster-7-scheduling-baseline.md) records source fingerprint,
commands, environment, complete comparison medians, stable digests, resource limits and guardrails.
The scheduled CLI example starts a large job, queues a higher-priority request and backfills a
smaller job, then completes all jobs. Waiting at queue exhaustion stays explicitly unfinished;
the CLI returns nonzero. Legacy explicit-assignment scenarios preserve their tested timing.

## Abstraction and scope review

[ADR-010](../adr/ADR-010-scheduler-placement-boundary.md) preceded implementation and defines
inventory ownership, admission rules, policy algorithms, failure semantics and bounds. Workloads
submit ordered workers to collectives; collectives still call Router, and transport owns timing.
Scheduling neither embeds routing decisions nor mutates network policy. Inventory validates the
policy's complete allocation before admitting compute. Custom policies can be injected without
changing the event engine. No new event variant or larger event envelope was needed.

Priority applies to the current waiting set; equal-time arrivals are not batched. There are no
reservations or preemption, and backfill has no starvation guarantee. Compact is greedy and can
lose to rack-local on makespan. Persistent GPU health belongs to scheduler inventory; job worker
failure remains a scoped abort, while fabric failure follows existing transport semantics.
Cancelled/failed jobs release GPUs while already-issued traffic drains, which can affect successors.

Waiting time ends at allocation; allocated GPU time excludes queueing. Failures before allocation
consume no GPU time. Fragmentation is a precisely defined locality metric, not an estimate of
unusable aggregate capacity. Record count and cumulative allocation IDs are separately bounded;
exhaustion fails explicitly. Inventory is capped at 8,192 GPUs, with no full graph copies in policies.

## Accepted limitations and next cluster

Planning was measured through 8,192 GPUs; training policy comparisons use 128 GPUs and 16/17 jobs.
Those tests do not certify arbitrary maximum-size workloads. NIC-local timing remains independent
without shared-bus contention, so placement gains are synthetic. Automatic topology-aware Ring
algorithms, GPU memory models, reservations, preemption and production scheduling protocols are
not required for this gate. Broader telemetry and failure replay belong to Clusters 8 and 9.

Milestone 5 is complete. Next: Milestone 6, starting with Cluster 8 Telemetry and Observability.
