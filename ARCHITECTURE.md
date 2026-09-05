<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab Architecture

## Status

This document describes the gate-approved architecture through Cluster 6. Subsystem details will
be added as their implementation clusters reach review. The
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
| Performance thresholds | Local post-baseline guardrails defined by architecture gates |
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

## Cluster 2 topology design

Cluster 2 uses distinct strong IDs and dense per-kind storage for GPUs, NICs, switches, racks, ports,
and physical links. Explicit ports make ownership and occupancy testable. Each full-duplex physical
link produces two deterministic directed arcs, providing the boundary for Cluster 3's per-direction
queue and service state.

The graph is topology-neutral. Direct, single-rack, leaf-spine, and Clos generators build the same
validated entity model. Failure changes explicit operational state without deleting entities or
renumbering IDs. Logical job placement remains a separate mapping to GPU IDs.

The initial deterministic two-tier Clos profile uses eight GPUs per NIC, eight NICs per leaf, and
eight spines: 512 GPUs produce 64 NICs and eight leaf/rack domains; the 2,048-GPU stretch profile
produces 256 NICs and 32 leaf/rack domains. Canonical topology interchange uses YAML; Graphviz DOT is
derived for visualization, while Protobuf remains deferred to Cluster 12.

See [ADR-005](docs/adr/ADR-005-topology-and-cluster-model.md) for topology identity, directionality,
validation, operational-state, generation, and serialization decisions.

Architecture Gate 2 accepted the topology model after the
[Cluster 2 topology baseline](docs/benchmarks/cluster-2-topology-baseline.md) identified repeated
port-occupancy scans and the measured
[linear-time validation optimization](docs/benchmarks/cluster-2-validation-optimization.md) removed
that scaling risk. The [gate](docs/architecture-gates/cluster-2.md) defines local 2,048-GPU
regression guardrails and preserves 8,192-GPU measurement for material topology changes.

## Cluster 3 transport design

Cluster 3 uses configurable chunk-level transfers over a runtime separate from the physical graph.
Each directed fabric arc owns an independent work-conserving FIFO serializer, a finite waiting
buffer, tail-drop accounting, and an optional marking threshold. Bandwidth, propagation latency,
buffer capacity, and chunk size are explicit integer experiment inputs rather than hardware
defaults.

Serialization and propagation are separate phases so the next queued chunk can begin service while
the prior chunk propagates. Routes are immutable per chunk; the initial helper uses one
caller-supplied path for a transfer, while Cluster 4 may select paths per chunk at admission.
Simulation-facing failures reconcile topology and transport state atomically, cancel active
service, and deterministically drop affected active and queued chunks without retransmission.

See [ADR-003](docs/adr/ADR-003-chunk-level-transfers.md) for the fidelity decision and
[ADR-006](docs/adr/ADR-006-link-queue-transfer-semantics.md) for queue admission, integer timing,
event ordering, failure, progress, and validation semantics.
[Architecture Gate 3](docs/architecture-gates/cluster-3.md) approves the implementation and records
the transport scale measurements.

Read-only transfer snapshots partition every registered byte and chunk into unscheduled, pending
arrival, waiting, active, propagating, delivered, or reason-specific dropped states. A final outcome
is retained and emitted once after every chunk becomes terminal; a drained event queue alone does
not imply a successful transfer. Per-direction statistics count accepted, started, serialized,
marked, and dropped traffic plus elapsed serializer busy time. Port and switch events use the same
critical-priority reconciliation as link failures. Recovery admits new work without retrying drops.

`TransportLimits` bounds retained chunks, route entries, and route length before allocation.
Snapshots cost O(chunks in the transfer); terminal accounting is incremental. Completed records
remain inspectable for the runtime lifetime, so experiments must fit the retained-state limits.
Allocation or event-ID exhaustion terminates a run; rollback and resumption are not promised.

## Cluster 4 routing design

The routing layer is separate from the event engine and transport runtime. `Router` selects a
registry-configured policy and submits its chosen fixed path through the existing transport API.
`PathService` enumerates the complete operational minimum-hop fabric path set with reverse BFS
and a strictly decreasing-distance walk. Canonical directed-link ordering makes ties reproducible.
Path/hop/cache bounds reject oversized sets without truncation. A FIFO pair cache invalidates
on effective link, port, or switch state changes, including recovery from cached disconnection.
Topology structure must remain fixed for the service lifetime; operational revisions are runtime
metadata and do not change canonical topology serialization.

Policies read a borrowed `FabricView` and candidate span synchronously. The view contains one
pointer and reads current per-arc waiting plus full active-chunk bytes; no fabric snapshot is copied.
Shortest-path chooses the first path, ECMP uses explicitly encoded versioned flow hashing,
least-loaded minimizes outstanding byte sums, and queue-aware minimizes a checked integer estimate
of first-chunk queue/service/propagation delay. Policies return candidate indices, which the router
validates, and cannot mutate simulation state through the interface. New policies register factories
without modifying the simulation engine.

Each admitted transfer has a pinned route. Failures cancel/drop existing affected work under
Cluster 3 rules; subsequent admissions select surviving paths. A disconnected request records a
no-route decision and creates no transfer. Decision records include request, policy/version,
simulated time, operational revision, candidate count, path, score, reason, and transfer identity.
The bounded record buffer can be drained; durable telemetry serialization remains later work.
Host policy execution time is measured outside deterministic decision records.

See [ADR-007](docs/adr/ADR-007-routing-policy-boundary.md) for alternatives, exact scoring and
bounds, and [Architecture Gate 4](docs/architecture-gates/cluster-4.md) for tests and measurements.

## Cluster 5 training workloads

`WorkloadEngine` schedules typed arrival/compute/control events for explicit ordered GPU
assignments. Each job has checked step/bucket dimensions, per-worker compute durations, arrival,
priority metadata and AllReduce parameters. Disjoint jobs run concurrently; assignment conflicts
fail without a hidden scheduling policy. Compute-to-bucket readiness supports an optional overlap
model; ordered bucket collectives and a step barrier determine the job critical path. Stragglers
come from worker-specific compute durations.

The engine depends on `CollectiveExecutor`, not a concrete ring planner. Job metrics distinguish
elapsed, allocated compute and idle GPU time, including partial intervals on cancellation/failure.
Timeline records link job/step/bucket transitions to collective IDs. Job failure cancels future
compute and stops future collective rounds; already-issued communication drains independently.
A job can finish before the simulation's final network-drain timestamp.

Strict versioned YAML scenarios and named synthetic parameter templates feed the `train` CLI.
In explicit-assignment mode, worker failure is a job-scoped abort and priority is metadata.
Cluster 7 optionally supplies scheduling; no hardware-calibrated compute/memory model is implied. [ADR-008](docs/adr/ADR-008-training-workload-lifecycle.md),
[Gate 5](docs/architecture-gates/cluster-5.md), and the [scenario guide](docs/training-scenarios.md)
record semantics and input/state limits.

## Cluster 6 collective execution

The pure Ring planner emits one round at a time: P−1 reduce-scatter rounds followed by P−1
all-gather rounds, with quotient/remainder shards. A global round barrier waits for all nonempty
transfers. The total successful logical volume is exactly `2(P−1)×gradient_bytes`; one participant
needs no communication. Tensor values and reduction arithmetic are not executed or timed.

`RingExecutor` maps GPUs to NICs and sends remote work through the existing Router and chunked
transport. Same-NIC work uses explicitly configured independent local transfer timing. Local and
fabric volumes remain distinct. Failed/no-route work stops later rounds; issued transfers drain
before the collective publishes one final outcome. Rank order comes from the caller; the planner
contains no routing policy. Pipelined channels, topology-aware rank ordering and shared local-bus
contention remain explicit deferrals.

`TrainingDispatcher` composes workload, collective and transport handlers and exclusively forwards
transport outcomes to their owner, then collective outcomes to the job engine. Unknown ownership
is an error. Workload and local-completion event kinds have stable trace codes; the event envelope
remains bounded. [ADR-009](docs/adr/ADR-009-ring-allreduce-execution.md) and
[Gate 6](docs/architecture-gates/cluster-6.md) record the execution boundary and measured costs.

## Cluster 7 admission and placement

Optional scheduling adds a bounded resource inventory and read-only SchedulingPolicy interface.
The workload engine owns waiting/admitted lifecycle transitions; inventory atomically validates
allocations. Placement selects GPU/rank order, while Router independently selects fabric paths.
Arrival, release and GPU health events trigger deterministic priority/arrival/ID queue passes with
non-reserving backfill. First-fit, seeded random, rack-local and compact policies expose decisions.
Persistent GPU down/up aborts owners and excludes failed resources until recovery. Job-scoped
worker failures retain the Cluster 5 behavior. Scheduling is opt-in, preserving explicit legacy runs.

Job snapshots separate arrival-to-allocation wait from allocated compute/idle. Queue exhaustion can
leave explicit waiting results. Decision records include locality and free-rack fragmentation;
retention limits bound both decision count and cumulative allocation IDs. Policies receive a borrowed
span, never a graph/runtime copy. See [ADR-010](docs/adr/ADR-010-scheduler-placement-boundary.md),
[Gate 7](docs/architecture-gates/cluster-7.md), and [scheduling guide](docs/scheduling.md).

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
