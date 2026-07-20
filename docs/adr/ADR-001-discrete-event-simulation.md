<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-001: Use Discrete-Event Simulation

## Status

Accepted — 2026-07-17

## Context

NexusLab must compare policies across large synthetic GPU clusters without spending wall-clock time on idle intervals or requiring real hardware. Results must be deterministic, replayable, and explainable at event boundaries.

## Options Considered

### Discrete-event simulation

Benefits:

- Advances directly between meaningful state changes.
- Supports deterministic ordering and controlled failure injection.
- Scales better than fixed-step simulation when activity is sparse.

Costs:

- Event volume and queue behavior require measurement.
- Simultaneous-event semantics must be specified carefully.

### Fixed-timestep simulation

Benefits:

- Simple global update model.
- Naturally supports regularly sampled state.

Costs:

- Wastes work during inactive periods.
- Accuracy and cost depend on an arbitrary timestep.

### Real-time emulation

Benefits:

- Can exercise real processes and protocols.

Costs:

- Requires substantially more resources.
- Makes deterministic, fast, large-scale comparisons harder.

## Decision

Use a single-process discrete-event simulation core for version one.

## Rationale

Discrete-event simulation best matches NexusLab's comparative goal and provides explicit control over ordering, causality, failures, and replay without pretending to reproduce hardware in real time.

## Consequences

Positive:

- Inactive simulated time has negligible execution cost.
- Identical inputs can produce stable event and result hashes.
- Event traces form a natural replay source.

Negative:

- The event queue may become a performance and memory bottleneck.
- Fidelity depends on the selected event abstractions.

## Validation

Cluster 1 must analytically test timestamp, priority, and sequence ordering; produce identical hashes across ten runs; and record throughput and memory measurements for one million and ten million no-op events.

## Revisit Trigger

Revisit if measured event volume prevents target-scale experiments after profiling and bounded optimizations, or if a required validation scenario cannot be represented without continuous-time or emulated behavior.
