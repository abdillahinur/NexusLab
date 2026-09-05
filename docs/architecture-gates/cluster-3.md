<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 3

Date: 2026-09-05. Scope: Link, Queue, and Transfer Model / Milestone 2 Fabric MVP.

## Acceptance checklist

| Requirement | Evidence | Result |
|---|---|---|
| Single-chunk analytical serialization and propagation | Timing, service, and runtime tests; exact integer arithmetic | Pass |
| Work-conserving FIFO and independent directions | Queue/service tests; serializer promotes the next chunk while the previous one propagates | Pass |
| Finite buffers, occupancy, tail drop, optional marking | Queue boundary tests and per-direction statistics tests | Pass |
| Fixed multi-hop routes and configurable chunk sizes | Runtime path validation, remainder chunk tests, benchmark sensitivity matrix | Pass |
| Transfer progress and byte/chunk conservation | Snapshot categories sum to registered total after every event | Pass |
| Exactly-once final success/failure | Terminal aggregation, loss waits for remaining chunks, drain-once completion records | Pass |
| Link, port, and switch failures | Critical events cancel active service and drain affected queues; overlapping recovery preserves failures | Pass |
| Deterministic outcomes | Repeated failure tests plus stable domain outputs across all benchmark repetitions | Pass |
| Invalid inputs and bounded state | Zero/overflow/path/ID tests; prevalidated retained chunk/route limits and timing | Pass |
| One million transport events | 333,333-chunk direct pipeline, exactly 1,000,000 dispatched events | Pass |
| Incast, all-to-all, 100 and 10,000 flows | Finite-buffer stress and buffered controls; complete 64-NIC ordered pair set | Pass |
| Generated Clos demonstration and queue buildup | `bash scripts/benchmark-transport.sh --pattern incast --flows 100` | Pass |

## Correctness and validation

GCC 13.3 and Clang 18.1.3 builds pass with warnings as errors. CTest passes **150/150** under GCC,
Clang, and Clang AddressSanitizer/UndefinedBehaviorSanitizer. The Clang build includes clang-tidy;
repository clang-format checks pass. Tests retain the topology correctness scales through
8,192 GPUs. The new CI transport benchmark smoke step is configured; hosted CI has not yet run
for this uncommitted change.

A transfer succeeds only when all its chunks are delivered. If any chunk drops, one failed outcome
is emitted when all chunks become terminal. Simulation queue exhaustion is a separate kernel
status and does not mean data delivery succeeded. Manual low-level registrations must be complete
before the first chunk is scheduled; unscheduled chunks remain visible and keep a transfer pending.

Statistics count traffic per directed hop. Accepted traffic includes immediate service;
completed bytes mean serialization completed, not destination delivery. Marks count each local
threshold decision. Busy time includes elapsed service up to a read, stop, or failure, rather than
crediting the full intended serialization of an interrupted chunk. Transfer snapshots partition
bytes without counting partial serialized bytes as delivered.

## Abstraction and extensibility

The physical graph remains separate from transport runtime and per-direction service. Typed
strong IDs, immutable caller-provided paths, explicit configurations, and read-only snapshots
provide the boundary for Cluster 4 routing. No topology-specific routing policy lives in runtime.
Progress accounting is incremental at terminal transitions; an on-demand progress snapshot scans
only that transfer's chunks. Completion records are consumed once and retained in snapshots.

Queue enqueue/dequeue are synchronous state transitions inside arrival/completion handlers, not
additional event envelopes. This implements the master-plan behavior with the two-phase event
model accepted by [ADR-006](../adr/ADR-006-link-queue-transfer-semantics.md). New port/switch event
kinds have explicit stable trace values; the kernel trace still describes event metadata, while
domain snapshots and counters establish transport determinism.

## Performance and memory

The [baseline](../benchmarks/cluster-3-transport-baseline.md) records the environment, source hashes,
raw measurements, reproducible commands, and local guardrails. One million transport events take a
median 476.677 ms and 113,968 KiB peak RSS. Buffered 10,000-flow incast and all-to-all complete all
flows without loss in 563.983 ms and 589.149 ms, with peak RSS below 66,000 KiB. Finite-buffer
stress exercises explicit drops. A 2,048-GPU incast and three chunk sizes extend the scale matrix.
One-million and ten-million no-op kernel controls pass; the event envelope remains 72 bytes.

These are synthetic ARM64 container measurements, not real-fabric performance or a comparison
against the older WSL2 baseline. No topology storage or algorithm changed, so the prior topology
performance measurements remain historical evidence rather than a new cross-host result.

## Failure behavior and accepted limits

- Simulation-facing link, port, and switch changes must use the runtime façade. Direct graph
  setters cannot cancel service and are only suitable outside an active transport simulation.
- The graph must outlive runtime and retain its structure. Local GPU-to-NIC attachments are not
  fabric serializers; the benchmark starts and ends at NICs. Zero-hop transfers are not supported.
- Failure drains active and waiting chunks on unavailable arcs. Already propagating chunks follow
  the accepted no-retroactive-loss rule and are checked at the next admission.
- Recovery admits new work; it does not resurrect dropped chunks. Reliable transport, retry,
  congestion response, and adaptive routing remain future work.
- Checked timing and ordinary validation happen before related mutations; allocation or event-ID
  exhaustion is fatal. Strong rollback and resumed failed runs are not promised.
- Retained-state limits bound chunks and route entries. Completed records remain for inspection;
  the model is not an unbounded streaming engine. Failure reconciliation scans configured arcs.
- Chunk-level FIFO/tail-drop behavior is sufficient for the present synthetic Fabric MVP claims.
  Packet mode and hardware calibration need new evidence before implementation.

## Documentation and decision

README, architecture, roadmap, master-plan milestone status, changelog, ADR-006 interface refinement,
benchmark instructions, raw results, and source hashes describe the completed implementation.
No training-workload, scenario CLI, routing-policy, or real-hardware claim is implied.

Proceed: **YES**

Required changes: **None for Cluster 3.**

Cluster 3 and Milestone 2 are complete. Cluster 4 routing-policy work may begin. This gate records
local engineering validation of the pending changes; the user will create the commit, and hosted CI
will run when those changes are pushed.
