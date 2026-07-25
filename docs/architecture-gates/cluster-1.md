<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 1

Date: 2026-07-25

## Correctness

- Simulated time is a strong unsigned nanosecond type with checked conversion and arithmetic.
- Events are value-owned typed payloads with engine-assigned IDs and optional cause IDs.
- Queue order is the total tuple of ascending timestamp, priority value, and event ID.
- Scheduling at the current simulated time is allowed; scheduling in the past is rejected.
- Cancellation uses deterministic lazy invalidation and does not advance time for discarded events.
- The single-use lifecycle returns explicit completed, stopped, or failed results.
- The deterministic RNG has golden-vector and bounded-sampling coverage.
- Typed traces use explicit stable field encoding and a project-owned 64-bit FNV-1a digest.
- Ten identical seeded simulations produce identical trace hashes.
- Forty-two CTest cases pass under GCC and Clang, including the benchmark smoke test.
- AddressSanitizer and UndefinedBehaviorSanitizer pass all 42 tests.
- The pushed Cluster 1 commit chain passed the hosted GitHub Actions workflow.

## Abstraction

- The queue owns `Event` values and does not retain caller pointers, references, or callbacks.
- Callers submit `EventSpec`; only the engine assigns event identity and causality.
- Handlers receive a bounded `SimulationContext` rather than direct clock or queue access.
- `std::variant` makes the internal payload set and dispatch exhaustiveness compile-time visible.
- Explicit payload-kind mapping prevents variant indexes from becoming trace identifiers.
- The queue implementation remains internal and can be replaced without changing event semantics.
- Topology, links, transfers, routing, workloads, replay, and control interfaces remain deferred to
  their owning clusters.

## Performance and Memory

The release benchmark disables tracing and measures scheduling, typed dispatch, result accounting,
event size, and resident memory. Three fresh processes were measured at each scale on the accepted
WSL2 reference environment.

| Events | Median insertion throughput | Median dispatch throughput | RSS insertion delta | Peak RSS |
|---:|---:|---:|---:|---:|
| 1,000,000 | 6,521,493.65 events/s | 5,216,992.67 events/s | 97,376 KiB | 100,736 KiB |
| 10,000,000 | 5,026,170.32 events/s | 4,612,945.08 events/s | 954,180 KiB | 1,277,788 KiB |

- `sizeof(Event)` is 56 bytes and `sizeof(EventPayload)` is 16 bytes.
- Steady insertion memory is approximately 99.71 bytes per event at one million events and 97.71
  bytes per event at ten million events.
- Ten million events complete without errors on the 16 GiB WSL2 reference environment.
- The ten-million-event peak shows temporary container-growth memory and must remain visible in future
  comparisons.
- The initial `std::priority_queue` implementation is sufficient for Cluster 2. No custom heap is
  justified before representative topology workloads are profiled.

Complete methodology and raw results are recorded in the
[Cluster 1 baseline](../benchmarks/cluster-1-baseline.md).

## Initial Regression Guardrails

These guardrails were defined after the baseline. They are local engineering alerts, not
hardware-neutral product claims or hard hosted-CI timing checks.

Evaluate the median of three fresh one-million-event release processes on the documented WSL2
reference environment:

- insertion throughput must remain at or above 4,500,000 events/s;
- dispatch throughput must remain at or above 3,600,000 events/s;
- RSS insertion delta must remain at or below 120,000 KiB;
- peak RSS must remain at or below 125,000 KiB;
- event size must remain at or below 96 bytes unless a gate explicitly accepts a larger budget.

The throughput floors preserve roughly a 30% margin below the measured medians. The memory ceilings
allow normal local variation while detecting structural growth. Ten-million-event results remain
required at architecture gates that materially change the event envelope, queue, or pending-event
tracking.

## Extensibility

- New payload alternatives must be explicit value types and add an explicit stable trace kind.
- Deterministic tracing can be enabled for evidence or disabled for honest throughput measurement.
- RNG state and trace records can be copied for deterministic tests.
- Event IDs and cause IDs provide stable integration points for future topology and transfer events.
- Known revisit triggers are documented for queue replacement, payload growth, cancellation memory,
  named RNG streams, and durable replay.

## Failure Behavior

- Timestamp and sequence overflow are rejected.
- Invalid past scheduling fails the simulation with preserved trace evidence.
- Unknown, repeated, dispatched, and completed-event cancellation requests return `false`.
- Stop requests are cooperative and valid only during event dispatch.
- Handler exceptions become failed results with final time, counts, trace hash, and error details.
- Invalid lifecycle reuse is rejected.
- Disabled tracing returns no trace hash and avoids trace metadata allocation and payload inspection.

## Checkpoint Answers

- **Does the simulator need cancellation now?** Yes. Later transfer, timeout, and failure models need
  deterministic invalidation. Lazy cancellation is accepted until representative cancellation
  frequency or retained memory justifies a different queue.
- **Are event payloads too large?** No. The current payload is 16 bytes and the complete event is 56
  bytes, below the initial 64-byte event budget. Every new payload must remeasure this.
- **Is the current queue implementation sufficient?** Yes. It completes ten million events with
  acceptable local throughput and memory. Representative fabric workloads will determine whether the
  queue becomes a dominant cost.
- **Is determinism enforced or merely expected?** Enforced. Strong time and ID types, a total
  comparator, engine-assigned causality, controlled RNG mapping, explicit trace encoding, and golden
  and ten-run tests make deterministic behavior executable.
- **Can every event be traced to its cause?** Yes. Bootstrap events explicitly have no cause; events
  scheduled during dispatch automatically record the current event ID.

## Accepted Risks and Deferrals

- Lazy cancellation retains heap entries until they reach the root.
- Payload growth affects every queued event and must be reviewed against the 96-byte budget.
- A single RNG stream means new draws can change later results.
- The local performance baseline is hardware-specific and does not support public comparative claims.
- Durable snapshots and replay serialization remain deferred to Cluster 12.
- Live streaming and cluster control remain outside the first release.

## Evidence Recorded

- Event-semantics decision: [ADR-004](../adr/ADR-004-deterministic-event-semantics.md).
- Baseline revision: `0a04cb4f4b66fcfd9e1ca37714b3d4021d30926f`.
- Baseline environment: Ubuntu 24.04.4 LTS on WSL2, AMD Ryzen 9 5900X, GCC 13.3.0.
- GCC and Clang builds with warnings as errors: passed.
- CTest under GCC and Clang: 42/42 passed.
- ASan/UBSan CTest: 42/42 passed.
- Linux clang-format check: passed.
- Linux clang-tidy build: passed.
- One-million and ten-million-event checksums: stable across all measured runs.
- Hosted GitHub Actions after publishing the baseline: reported successful on 2026-07-25.

## Decision

Proceed: **YES**

Required changes:

- None.

Cluster 1 is approved. Cluster 2 topology and cluster-model work may begin under the master plan.
