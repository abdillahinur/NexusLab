<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-002: Use Integer Nanoseconds for Simulated Time

## Status

Accepted — 2026-07-17

## Context

Event ordering and replay hashes must remain stable across runs, compilers, and supported machines. Floating-point timestamps can introduce rounding ambiguity and platform-dependent comparisons.

## Options Considered

### Unsigned 64-bit integer nanoseconds

Benefits:

- Exact ordering and arithmetic within the supported range.
- Nanosecond precision is sufficient for the planned model.
- Straightforward serialization and hashing.

Costs:

- Conversions and overflow must be checked.
- Fractional nanoseconds cannot be represented.

### Floating-point seconds

Benefits:

- Convenient unit conversions and fractional values.

Costs:

- Equality and ordering can be affected by rounding.
- Stable cross-platform replay is harder to guarantee.

### Strong duration types with mixed periods

Benefits:

- Compile-time unit safety.

Costs:

- A canonical storage unit is still required for ordering and serialization.

## Decision

Store canonical simulated timestamps as `std::uint64_t` nanoseconds and provide explicit checked conversion helpers. Strong wrapper types may be introduced without changing the underlying unit.

## Rationale

Integer nanoseconds make deterministic ordering and serialization explicit while covering the planned duration range by a wide margin.

## Consequences

Positive:

- No floating-point timestamps enter the core event queue.
- Equal timestamps can be ordered by priority and deterministic sequence.

Negative:

- Overflow and invalid conversion behavior must be designed and tested.
- External floating-point inputs require validation and quantization.

## Validation

Cluster 1 must test conversions, boundary values, overflow rejection, monotonic time, and deterministic ordering for identical timestamps.

## Revisit Trigger

Revisit only if a validated scenario requires sub-nanosecond precision or durations beyond the unsigned 64-bit nanosecond range.
