<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-004: Define Deterministic Event Semantics

## Status

Accepted — 2026-07-25

## Context

Cluster 1 must provide the simulation kernel used by every later NexusLab subsystem. The kernel must make ordering, ownership, cancellation, random-number generation, lifecycle, and causality deterministic by construction while remaining small enough to benchmark honestly before optimization.

The design must also avoid event payloads that own or point directly to future topology, workload, or policy objects. Later clusters should exchange stable identifiers and value data through the event system rather than coupling the queue to mutable domain-object lifetimes.

## Options Considered

### Event payload representation

#### Typed value payloads in `std::variant`

Benefits:

- Provides compile-time exhaustiveness and explicit event kinds.
- Avoids one allocation and one virtual dispatch per event.
- Keeps trace and hash encoding inspectable.

Costs:

- Adding a new internal event kind updates the central payload variant and dispatcher.
- The largest alternative affects the size of every queued event.

#### Polymorphic payload objects

Benefits:

- New payload classes can be introduced without modifying a central variant.

Costs:

- Requires dynamic allocation or a custom allocator strategy.
- Introduces virtual dispatch and more complicated ownership.
- Makes stable tracing and hashing dependent on extra conventions.

#### Type-erased callbacks

Benefits:

- Provides a compact scheduling API.
- Lets callers capture arbitrary state.

Costs:

- Captures can hide mutable ownership and nondeterministic behavior.
- Callback identity and state are difficult to trace, hash, or replay.
- `std::function` may allocate and has no stable serialization model.

### Queue ownership

#### Events stored by value

Benefits:

- The simulation owns every queued event.
- No queued references can dangle.
- Copy, move, size, and allocation behavior can be measured directly.

Costs:

- Payload size must remain bounded.

#### Caller-owned events referenced by the queue

Benefits:

- Small queue entries.

Costs:

- Lifetime errors and mutation after scheduling threaten correctness and determinism.

### Initial queue implementation

#### `std::priority_queue` backed by `std::vector`

Benefits:

- Well-understood logarithmic insertion and removal.
- Minimal custom code before measurements exist.

Costs:

- Does not support efficient arbitrary removal.
- May eventually be outperformed by a workload-specific queue.

#### Custom heap or calendar queue

Benefits:

- May improve throughput for particular timestamp distributions.

Costs:

- Adds correctness and maintenance risk before profiling identifies a bottleneck.

## Decision

### Event envelope and ownership

The simulation owns queued events by value. An event contains:

- a `SimTimeNs` timestamp backed by `std::uint64_t`;
- an `EventPriority` backed by an unsigned integer, where lower values execute first;
- an engine-assigned, monotonically increasing `EventId` that also serves as the final ordering sequence;
- an optional causing `EventId`;
- a typed `EventPayload` represented by `std::variant`.

Payload alternatives are small value types containing stable identifiers, enums, and scalar data. Payloads must not contain owning pointers, non-owning references, arbitrary callbacks, wall-clock timestamps, or floating-point simulated time.

Callers submit an event specification without an ID or sequence. `Simulation::schedule` validates it, assigns the next `EventId`, records causality from the currently executing event when applicable, and returns the assigned ID. Sequence overflow is a fatal simulation invariant violation.

### Total ordering

Queued events use one total lexicographic order:

1. ascending timestamp;
2. ascending priority value;
3. ascending `EventId`.

Scheduling in the simulated past is rejected. Scheduling at the current timestamp is allowed. An event scheduled during dispatch receives a later ID, but its priority still participates in the documented ordering tuple.

### Queue and cancellation

Cluster 1 begins with `std::priority_queue` backed by `std::vector` and an explicit comparator implementing the total order.

Cancellation uses lazy invalidation:

- `cancel(EventId)` marks a queued event ID invalid;
- cancelled entries remain in the heap until they reach the top;
- a cancelled event is discarded without dispatch and without advancing simulated time;
- cancelling an unknown, already dispatched, or already cancelled ID reports `false`;
- cancellation state is queried by membership only and is never iterated to determine behavior.

This avoids a custom indexed heap before cancellation frequency is measured.

### Dispatch and causality

Dispatch uses an explicit `std::visit`-based handler over the typed payload variant. Handlers receive a bounded simulation context that can read the current time and event ID, draw deterministic random values, schedule or cancel events, and request a stop. Handlers cannot mutate the clock or queue directly.

Events scheduled during a handler automatically record that handler's event as their cause. External bootstrap events have no cause.

### Lifecycle and results

A simulation is single-use and moves through:

```text
Created -> Running -> Completed | Stopped | Failed
```

- An empty queue completes successfully.
- `stop` is cooperative: the current handler finishes, then no further event is dispatched.
- Invalid lifecycle transitions are rejected.
- Handler failures produce a failed result and preserve the trace recorded up to the failure.

`SimulationResult` records the terminal status, stop reason when present, final simulated time, dispatched and cancelled-event counts, pending-event count, RNG draw count, stable trace hash, and error details when failed.

### Deterministic random numbers

The simulation owns a deterministic RNG wrapper seeded by an explicit `std::uint64_t`. The wrapper uses the raw output of `std::mt19937_64` and project-owned integer rejection sampling rather than standard-library distribution classes, whose mapping is not required to be identical across implementations.

Cluster 1 exposes raw 64-bit values and unbiased bounded integers only. Floating-point distributions and subsystem-specific random streams are deferred until a validated use case requires them.

### Trace and deterministic digest

Basic tracing records scheduling, dispatch, cancellation, stop, completion, and failure with event ID, cause, timestamp, priority, and explicit payload kind. Tracing can be disabled for throughput benchmarks.

Determinism tests use a project-owned, non-cryptographic 64-bit FNV-1a digest over explicitly encoded trace fields. The digest never uses `std::hash`, addresses, padding bytes, locale-dependent text, or `std::variant` alternative indexes.

Full queue and domain-state snapshots are deferred. Cluster 1 provides deterministic trace and RNG state copying for tests; replay and durable snapshot formats remain owned by Cluster 12.

## Rationale

Typed value events make behavior, ownership, and hashing explicit without paying for per-event allocation or permitting hidden callback state. The ordering tuple removes ambiguity at equal timestamps, while engine-assigned IDs provide both stable tie-breaking and trace identity.

`std::priority_queue` and lazy cancellation are intentionally conservative starting points. Cluster 1 exists to establish the event-volume and memory baseline that can justify a different data structure with evidence.

## Consequences

Positive:

- Event order is total, documented, and independent of pointer values.
- The queue owns its data and cannot retain caller references.
- Every dispatched event can be traced to a stable ID and optional cause.
- RNG mappings used by the core are controlled by NexusLab.
- Queue replacement remains an internal optimization if semantics are preserved.

Negative:

- The payload variant and dispatcher are central internal integration points.
- Lazy cancellation retains invalid entries until they reach the heap root.
- Event size grows to match the largest payload alternative.
- A single RNG stream means added draws can change later results; named streams may be needed in a later cluster.
- Durable snapshots and replay are not available in Cluster 1.

## Validation

Cluster 1 must:

- test timestamp, priority, and ID ordering independently and together;
- test same-timestamp scheduling during dispatch;
- reject past scheduling and sequence overflow;
- test successful, stopped, failed, and empty-queue results;
- test cancellation before dispatch and invalid cancellation requests;
- verify automatic cause IDs;
- verify RNG golden vectors and unbiased bounded-range edge cases;
- verify ten identical seeded runs produce identical trace hashes;
- run cleanly under ASan and UBSan;
- record event size, memory use, insertion throughput, and dispatch throughput for one million and ten million no-op events.

The first measured results establish the baseline. Regression thresholds are defined afterward, not invented before measurement.

## Revisit Trigger

Revisit this decision if measurements show that:

- queue operations dominate representative simulation runtime;
- lazy cancellation produces unacceptable retained memory;
- the event payload exceeds the measured size budget established at Gate 1;
- the central variant prevents a required subsystem boundary;
- one global RNG stream makes controlled policy comparisons impractical; or
- a validated replay requirement needs durable snapshots before Cluster 12.
