<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab Architecture

## Status

This document describes the approved architectural direction through Cluster 1. Subsystem details
will be added only as their implementation clusters reach review. The
[master engineering plan](NEXUSLAB_MASTER_PLAN.md) remains the source of truth.

## System boundary

NexusLab is a single-process, deterministic discrete-event simulator for comparative experiments on synthetic AI training infrastructure. The simulation core must remain usable without the future web backend. Production integrations are future backend adapters and must not leak simulated internals into controller interfaces.

## Planned component flow

```text
scenario definition
  -> experiment orchestrator
  -> deterministic simulation core
  -> cluster/fabric and workload engines
  -> pluggable policy layer
  -> telemetry and metrics
  -> result store and replay
  -> CLI, reports, and replay dashboard
```

## Approved initial decisions

| Area | Decision |
|---|---|
| Core language | C++20 |
| Required development target | Linux or WSL2 |
| Build system | CMake with Ninja presets |
| Tests | GoogleTest and GoogleMock 1.17.0 |
| Human-authored configuration | YAML through yaml-cpp 0.9.0 |
| Replay serialization | Protobuf deferred until Cluster 12 |
| Repository model | Monorepo |
| Simulation style | Single-process discrete-event simulation |
| Simulated time | Integer nanoseconds |
| Transfer fidelity | Chunk level |
| Initial topology | Clos |
| Scale targets | 512 GPUs initial; 2,048 stretch |
| Initial collective | Ring AllReduce |
| MVP routing | ECMP, least-loaded, queue-aware |
| Initial scheduler | First fit |
| Anchor failure | Spine-link failure |
| Headline metrics | Job completion time, GPU idle time, queue depth, link utilization, drops |
| First UI release | Replay only; no live streaming or cluster control |
| Performance thresholds | Set after the Cluster 1 local baseline |
| Portfolio-ready MVP target | 16 weeks, subordinate to architecture-gate quality |
| License | Apache-2.0 |

## Determinism boundary

Given identical configuration, seed, build version, and policy version, a run must produce identical output unless nondeterministic execution is explicitly enabled. Event ordering, random-number generation, identifiers, policy tie-breaking, and serialization must therefore be deterministic by construction.

## Cluster 1 simulation-core design

Cluster 1 uses value-owned typed events with a total order of timestamp, priority, and engine-assigned event ID. The event ID also acts as the final sequence tie-breaker and trace identity. Event payloads use `std::variant` and contain stable identifiers and scalar values rather than pointers, references, or arbitrary callbacks.

The initial queue is `std::priority_queue` backed by `std::vector`. Cancellation uses lazy invalidation until measurements justify an indexed or custom queue. Dispatchers receive a bounded context for scheduling, cancellation, deterministic random draws, and cooperative stop requests; they cannot mutate the clock or queue directly.

The deterministic RNG uses raw `std::mt19937_64` output with project-owned bounded-integer sampling. Trace hashes use explicit field encodings and never depend on addresses, padding, `std::hash`, or variant indexes. Full durable snapshots and replay remain deferred to Cluster 12.

See [ADR-004](docs/adr/ADR-004-deterministic-event-semantics.md) for the complete event, lifecycle, cancellation, tracing, and validation semantics.

Architecture Gate 1 accepted the initial queue and lazy-cancellation strategy based on the measured
[Cluster 1 performance baseline](docs/benchmarks/cluster-1-baseline.md). The gate defines local
one-million-event regression guardrails and preserves ten-million-event measurement for material
changes to the event envelope, queue, or pending-event tracking.

## Policy boundaries

Routing, scheduling, congestion control, collective planning, and failure recovery will use stable replaceable interfaces. Policies receive bounded views or snapshots, do not mutate simulation state directly, and emit inspectable decision records.

## Data and safety boundaries

- Initial workloads and results are synthetic.
- The initial release replays completed experiments and does not control a live cluster.
- Scenario sizes and inputs must be validated before allocations occur.
- Any future real-cluster action requires an explicit operating mode and independent safety controls.

## Cluster 0 repository shape

```text
NexusLab/
├── cmake/                  build policy and pinned dependencies
├── docs/adr/               architecture decision records
├── scripts/                Linux/WSL2 developer workflows
├── simulator/              C++ targets, tests, and benchmarks
├── .github/                CI and contribution templates
├── CMakeLists.txt
└── CMakePresets.json
```

Directories for policies, scenarios, schemas, services, web, and analysis will be introduced by the clusters that own them, avoiding empty scaffolding that implies unsupported behavior.

## Architecture gates

Every cluster ends with a written review of correctness, abstraction, performance, extensibility, failure behavior, and documentation. A gate records evidence and an explicit `Proceed: YES` or `Proceed: NO`; elapsed schedule time cannot waive it.
