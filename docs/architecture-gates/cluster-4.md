<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 4

Date: 2026-09-05. Scope: Routing Policy Framework / Milestone 3 Routing Comparison.

## Acceptance checklist

| Requirement | Evidence | Result |
|---|---|---|
| Route representation and shortest-path enumeration | Directed-hop paths; reverse BFS and complete canonical minimum-hop candidates | Pass |
| Known graph correctness and no loops | Direct, Clos through 8,192 GPUs, and cyclic non-Clos longer-path recovery tests | Pass |
| Static shortest path and stable ECMP | Canonical ties, explicit versioned hash golden vector, all eight paths exercised | Pass |
| Least-loaded and queue-aware baselines | Outstanding-byte and checked delay scoring; loaded heterogeneous-link preference test | Pass |
| Replaceable configuration-selected policies | Registry factories, stable names, unknown/duplicate/invalid factory tests, CLI `--policy` | Pass |
| Read-only policy boundary | Borrowed view/candidate span; validated candidate index; no policy references in event engine | Pass |
| Decision recording | Request, policy/version, timestamp, revision, candidates, path, score/reason, transfer identity | Pass |
| Failures trigger valid new routing | Link, port, and switch events invalidate cached paths before subsequent admissions | Pass |
| Disconnection and recovery | Explicit no-route records without transport mutation; negative cache invalidates on recovery | Pass |
| Bounded memory and snapshot cost | Path/hop/pair/entry limits, drainable decision cap, eight-byte borrowed view, measured cache payload/RSS | Pass |
| Reproducible comparisons | Four policies, identical traffic/seed, 96 processes with stable domain results across repetitions | Pass |

## Correctness and quality

**171/171 CTest checks pass** under GCC 13.3, Clang 18.1.3 with clang-tidy, and Clang
AddressSanitizer/UndefinedBehaviorSanitizer. Both compilers use warnings as errors. Repository
clang-format and `git diff --check` pass. Existing kernel, transport, topology, import/export, and
CLI tests remain intact. A routing benchmark smoke test is included in CTest and the hosted CI
workflow. Hosted CI has not run for these uncommitted changes.

The configured validation builds use `/tmp/build-gcc`, `/tmp/build-lint`, `/tmp/build-sanitize`,
and `/tmp/build-release` in the Linux container. Each was built with `cmake --build DIRECTORY`
and the first three tested with `ctest --test-dir DIRECTORY --output-on-failure`.
`bash scripts/format.sh --check` verifies repository formatting. Equivalent fresh builds use the
existing `dev`, `lint`, `sanitize`, and `release` CMake presets with the corresponding compiler.

Shortest-path distance strictly decreases on every enumerated hop, so cycles cannot enter the
candidate set. Enumeration limits reject the whole request rather than silently biasing ECMP with
partial candidates. A policy selects a validated candidate index and cannot inject an arbitrary
path. Checked integer sums and serialization delays reject overflowing scores.

Operational revisions advance only on valid effective state changes. Cached successful and
unsuccessful lookups invalidate automatically on link, port, and switch failure/recovery. Invalid
and redundant state assignments preserve the revision. Revision exhaustion rejects a further
state change before mutation. This runtime metadata does not alter canonical topology export.

## Abstraction and extensibility

[ADR-007](../adr/ADR-007-routing-policy-boundary.md) records the interface and design alternatives.
The router owns lookup, policy invocation, transport submission and bounded decision records;
policies own only route choice. Native factories extend the registry without changing engine
code. The transport runtime retains the fixed-route API from Cluster 3.

Routing is per transfer at admission. All chunks of that transfer share the selected path. A
logical flow key is supplied before transport allocates its separate transfer ID. The seed, policy
version, endpoints, and flow key define ECMP selection without consuming simulation RNG state.
Policy execution time is measured by the benchmark and does not affect simulated time or decisions.
The illustrative master-plan confidence field is omitted because there is no calibrated confidence
model; the accepted refinement uses an integer score and explicit reason instead.

The fabric view is borrowed and synchronous; it neither copies the graph nor owns telemetry.
Current waiting bytes plus the full active chunk provide a conservative service-work estimate.
The graph and runtime must outlive the router, refer to the same fabric, and retain their structure.
Thread-safe concurrent mutation is outside the single-threaded simulation contract.

## Performance, observability, and comparison

The [routing baseline](../benchmarks/cluster-4-routing-baseline.md) records source fingerprint,
methodology, curated measurements, exact traffic configuration, and local regression guardrails.
There are 32 distinct cases across three fresh processes each: lookup/selection at
64/512/2,048/8,192 GPUs, incast and all-to-all comparisons, a 2,048-GPU traffic case, and 10,000 flows.
All 96 processes passed conservation checks and repeated their domain output exactly.

At 2,048 GPUs, cold path lookup measured 31.625–44.084 µs across policy runs, warm lookup
9.8–12.0 ns, and queue-aware idle selection 3.317 µs. The filled cache's directed-hop payload is
127,200 bytes, with process peak RSS 5,116 KiB. At 8,192 GPUs, payload is 520,416 bytes and process
peak RSS is approximately 11,040 KiB. Payload is not total cache memory: RSS includes containers,
transport, topology, and temporary allocation. The cache is lazy and bounded, not all-pairs storage.

Incast has a 93.7% cache-hit rate in the 1,000-flow test; the all-to-all sequence has no hits within
its reuse window. Cache benefit therefore depends on workload rather than being assumed.

ECMP completes all 10,000 all-to-all transfers with zero drops in the measured synthetic workload.
Least-loaded and queue-aware reduce dropped bytes relative to the canonical static path but
complete fewer transfers than ECMP. Their outcomes coincide on these homogeneous links. The report
preserves this result and the heavy incast loss; it does not claim a universal policy winner.
Successful-transfer latency percentiles must be read alongside successful counts and dropped bytes.

## Checkpoint answers and accepted limits

- **Is the snapshot expensive to copy?** No whole-fabric copy occurs. A view occupies one pointer;
  policies pay per-hop map lookup and scoring cost, which is measured separately.
- **Current or delayed telemetry?** Current queue state at synchronous admission. There is no
  configurable observation delay or reservation for traffic still propagating toward a hop.
- **Does least-loaded oscillate?** A transfer's path is pinned. Later admissions can change choices
  as queues change, and canonical ties can concentrate traffic before downstream queues reflect it.
- **Are flowlets needed now?** No for the accepted fixed-transfer comparison. Reordering, flowlet
  state, reservations, and hysteresis require explicit future evidence and an ADR.
- **What does failure rerouting mean?** New admissions use surviving paths. Already affected chunks
  keep Cluster 3's drop/no-retry behavior; this gate does not claim mid-transfer reliable recovery.
- **What if no path or limits are reached?** No path records a decision and returns no submission.
  Invalid endpoints, oversized path sets, score overflow, and full decision buffers fail explicitly.
  The decision buffer can be drained. Allocation failure remains fatal, with no strong rollback.
- **What is deferred?** GPU-local/zero-hop transfer handling, scenario-driven workload CLI, durable
  telemetry storage, real-hardware calibration, adaptive in-flight routing and congestion control.

## Documentation and decision

README, architecture, ADR-007, roadmap, master-plan interface/milestone/tracker, changelog and the
curated benchmark report describe this implementation. The stale Cluster 3 tracker row is also
aligned with its already-approved gate. No dependencies, build artifacts, or local raw logs are added.

Proceed: **YES**

Required changes: **None for Cluster 4.**

Cluster 4 and Milestone 3 are complete within the recorded admission-routing boundary. Cluster 5
training-workload design may begin. The user will create the commit; no commit or push was performed
by the agent, and hosted CI remains pending.
