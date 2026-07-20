<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-003: Use Chunk-Level Transfers

## Status

Accepted — 2026-07-17

## Context

NexusLab must expose queue buildup, serialization delay, drops, and collective progress while scaling to hundreds and eventually thousands of simulated GPUs. Packet-level modeling would generate excessive event counts for version one, while pure flow-level modeling would hide useful queue behavior.

## Options Considered

### Packet-level transfers

Benefits:

- Highest network-detail potential.

Costs:

- Billions of events are plausible for target workloads.
- Encourages unsupported claims of hardware-level realism.

### Chunk-level transfers

Benefits:

- Preserves queueing, buffering, serialization, and collective dependencies.
- Provides configurable fidelity and bounded event volume.

Costs:

- Cannot represent every packet-level protocol behavior.
- Results may be sensitive to chunk size.

### Flow-level transfers

Benefits:

- Lowest event and memory cost.

Costs:

- Hides burst structure, finite buffers, and chunk dependencies.

## Decision

Use configurable chunk-level transfers for the MVP. A chunk may represent a gradient-bucket segment, fixed transfer quantum, or collective message fragment. Packet-level mode is not part of version one.

## Rationale

Chunk-level simulation provides the minimum useful fidelity for NexusLab's comparative queueing and collective experiments without making the target scale impractical from the outset.

## Consequences

Positive:

- Queue occupancy, buffer overflow, and link service remain observable.
- Fidelity can be explored through documented chunk-size sensitivity tests.

Negative:

- Results cannot be presented as packet-accurate RDMA behavior.
- Chunk-size configuration becomes part of experiment provenance.

## Validation

Cluster 3 must compare isolated transfer times with analytical expectations, test queue and drop accounting, benchmark event counts across chunk sizes, and publish sensitivity results.

## Revisit Trigger

Revisit if a documented research question fundamentally requires packet semantics, or if profiling shows that an alternate flow-level backend is required for specific scale experiments.
