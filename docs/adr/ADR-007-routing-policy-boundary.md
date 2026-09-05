<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-007 — Deterministic routing at transfer admission

Date: 2026-09-05. Status: Accepted; [Architecture Gate 4](../architecture-gates/cluster-4.md) approved.

## Decision and alternatives

Select one operational, minimum-hop fabric path per transfer at submission and pin its chunks to
that path. Per-chunk and flowlet selection add reordering and state without an MVP workload that
requires them. Existing in-flight loss retains Cluster 3 semantics: no retransmission or in-flight
path rewriting. New submissions after link, port, or switch failure select an alternative when one
exists; disconnected requests return an explicit no-route decision without creating a transfer.
Endpoints are distinct NICs or switches. GPU-local and zero-hop handling remain workload concerns.

A topology-neutral PathService uses reverse BFS and enumerates the shortest-path DAG in stable
lexicographic directed-link order. Strictly decreasing distance prevents loops. Enumeration is
bounded and fails explicitly if the complete candidate set exceeds configured path/hop limits;
ECMP must never silently sample a truncated set. A bounded FIFO pair cache stores only paths,
never queue scores. Operational revision changes invalidate all entries, including no-path entries.
The graph structure must remain fixed while the service exists. State setters advance a checked
revision only on actual changes; redundant/invalid updates do not invalidate caches.

Policies receive a const span of candidates and a borrowed read-only fabric view; no whole-fabric
snapshot is copied. The dispatcher must call synchronously on the simulation thread. Link reads
include waiting bytes, the full active chunk's bytes, bandwidth and propagation. No delayed
telemetry, reservation, or future traffic is implied. Batched submissions before arrival events
see the same queues; comparison traffic must declare arrival spacing.

Version 1 policies are configuration-selected through an extensible name/factory registry:

- `shortest-path`: lexicographically first minimum-hop path.
- `ecmp`: FNV-1a over explicitly encoded little-endian version, seed, flow key, endpoint kinds and
  IDs, modulo the complete ordered equal-hop candidate count. No `std::hash` or shared RNG state.
- `least-loaded`: minimize the sum of waiting plus full active-chunk bytes over the path.
- `queue-aware`: minimize the sum of checked integer serialization delays for outstanding bytes
  plus the first requested chunk, and propagation delay, at each hop. This is a queue-delay
  estimate, not predicted end-to-end transfer completion time.

Score ties choose the first canonical path. Whole active-chunk accounting is a conservative
instantaneous load estimate; partial serializer progress is not inferred. Least-loaded and
queue-aware can coincide on homogeneous links; heterogeneous-link tests establish their distinct
semantics. Pinned transfers cannot oscillate between routes. Later submissions may switch as
current queues change; hysteresis and flowlets are deferred.

Policies return a candidate index, score and reason; the router validates the index, preventing a
plugin from injecting a loop or off-candidate path. Domain decision records retain request,
policy/version, timestamp, operational revision, candidate count, selected path, score, reason,
and optional submitted transfer ID. No-path decisions are retained too. A bounded drainable
record buffer fails before further routing when full. Host execution time is benchmark output,
not simulation state or deterministic telemetry.

Defaults: 64 candidates/pair, 64 hops, 1,024 cached pairs, 262,144 cached route entries, 100,000
undrained decisions. Limits must be positive, with at most 1,024 configured hops; excessive enumeration fails without a partial cache
entry. Cache misses may evict FIFO entries until the route-entry budget fits. There is no all-pairs
precomputation. Allocation failure is fatal, consistent with Cluster 3. No new dependencies.

## Validation plan

Known direct/Clos paths; loop and failed-component exclusion; failure/recovery cache invalidation;
stable ECMP golden vector; score/tie behavior on heterogeneous loaded links; invalid endpoints,
limits and plugin results; registry configuration; decision records and no-route handling;
transport integration after failure; repeated domain outcomes. Measure cold/warm lookup, cache
memory, selection cost and identical seeded traffic across policies at 64/512/2,048/8,192 GPUs.
Document results and snapshot cost before approving Architecture Gate 4.
