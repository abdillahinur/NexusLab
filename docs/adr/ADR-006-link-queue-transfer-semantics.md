<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-006: Define Link, Queue, and Transfer Semantics

## Status

Accepted — 2026-07-27

## Context

Cluster 3 must turn Cluster 2's directed topology arcs into a deterministic data-movement model
without embedding workload, routing-policy, or telemetry behavior in the physical graph. The model
must expose serialization delay, propagation delay, queue buildup, finite buffers, drops, optional
congestion marking, and transfer progress while remaining practical at the approved 512-GPU initial
and 2,048-GPU stretch targets.

[ADR-003](ADR-003-chunk-level-transfers.md) already selects configurable chunk-level fidelity.
[ADR-004](ADR-004-deterministic-event-semantics.md) requires value-owned event payloads with stable
identifiers, and [ADR-005](ADR-005-topology-and-cluster-model.md) provides a stable
`DirectedLinkId` for each direction of a physical link. This decision defines how those boundaries
interact.

The first implementation must not invent hardware performance defaults or imply packet-accurate
RDMA behavior. Bandwidth, latency, buffer, chunk-size, and marking values are explicit experiment
inputs and provenance.

## Options Considered

### Transport-state ownership

#### Separate runtime keyed by directed-link ID

Benefits:

- Preserves physical topology across transport experiments.
- Gives each full-duplex direction independent queue and service state.
- Keeps mutable transfer lifecycles out of topology serialization.
- Lets routing and telemetry consume bounded transport views later.

Costs:

- Operational-state changes must reconcile topology and transport state atomically.
- Configuration must cover every modeled directed arc.

#### Store queue and transfer state in `TopologyGraph`

Benefits:

- One lookup reaches connectivity and current queue state.

Costs:

- Couples reusable topology to simulation lifecycle and experiment configuration.
- Makes canonical topology YAML contain transient behavior.
- Forces routing queries to depend on transfer implementation details.

### Queue discipline

#### Work-conserving FIFO with tail drop

Benefits:

- Has explicit, deterministic ordering from simulation arrival order.
- Exposes queue buildup and finite-buffer overflow with a small state model.
- Provides a trustworthy baseline before policy or congestion-control comparisons.

Costs:

- Does not model priority classes, weighted fairness, or packet scheduling.
- Coarse chunks can produce visible head-of-line blocking.

#### Per-flow fair queueing

Benefits:

- Reduces head-of-line blocking across flows.

Costs:

- Introduces a scheduling policy before a measured baseline exists.
- Requires additional flow-round accounting and tie-breaking rules.

#### Infinite buffer

Benefits:

- Simplest service model.

Costs:

- Cannot exercise tail drop or finite-buffer congestion.
- Hides an explicit Cluster 3 acceptance requirement.

### Transfer timing

#### Separate serialization completion and propagation arrival

Benefits:

- Matches the analytical model
  `arrival = service_start + serialization_delay + propagation_delay`.
- Allows the serializer to begin the next queued chunk while the prior chunk propagates.
- Makes queueing delay, serialization delay, and propagation delay independently observable.

Costs:

- Requires two event transitions per traversed hop.

#### One combined per-hop delay

Benefits:

- Uses fewer event kinds.

Costs:

- Holds the serializer during propagation or hides when it becomes available.
- Cannot explain queueing and propagation independently.

### Drop behavior

#### Atomic chunk drop without retransmission

Benefits:

- Keeps byte accounting nonnegative and deterministic.
- Makes loss visible without inventing a transport protocol.
- Leaves retransmission and congestion response to an explicit future model.

Costs:

- Any dropped chunk prevents successful transfer completion.
- The model cannot claim reliable-transport completion under loss.

#### Automatic retry

Benefits:

- Can eventually complete transfers after transient loss.

Costs:

- Requires timeout, acknowledgment, backoff, and retry-limit semantics.
- Would imply protocol fidelity that the MVP does not implement.

## Decision

### Identity, quantities, and ownership

Cluster 3 introduces strong unsigned value types for transfer ID, chunk ID, bytes, and bits per
second. Zero-byte transfers, zero-byte chunk limits, and zero bandwidth are invalid. All byte and
time arithmetic is checked before state mutation.

A transport runtime is separate from `TopologyGraph` and is keyed by
`topology::DirectedLinkId`. It owns:

- immutable per-direction configuration;
- one queue and one serializer state per modeled directed arc;
- transfer and chunk progress records;
- deterministic transfer and chunk ID generators;
- aggregate queue, service, mark, and drop counters.

The initial runtime models only `LinkKind::Fabric` arcs. GPU-to-NIC local attachments remain
topological relationships rather than timed, buffered resources. Host injection, PCIe, or local-link
contention requires a later explicit decision rather than silently reusing fabric parameters.

Configuration is not stored in canonical topology YAML. Each modeled direction requires explicit:

- bandwidth in integer bits per second;
- propagation latency in integer nanoseconds;
- waiting-buffer capacity in bytes;
- an optional marking threshold in waiting bytes.

The marking threshold, when enabled, must be nonzero and must not exceed buffer capacity. Opposite
directions may use different configuration even though the initial Clos scenarios normally
configure them symmetrically.

### Chunking and routes

A nonzero transfer byte count is partitioned deterministically into chunks no larger than the
configured maximum. Chunk IDs follow transfer submission and chunk ordinal order; the final chunk
may be smaller. Submission validates size, chunk limit, route, and arithmetic before assigning
transfer or chunk IDs or scheduling events.

Each chunk carries an immutable, ordered path of directed fabric-link IDs. Cluster 3 validates that
the path is nonempty, contiguous, uses known fabric arcs, and has the expected source and
destination. The Cluster 3 helper assigns the same caller-supplied path to every chunk in a
transfer. Cluster 4 may choose a path per chunk at admission without changing queue or service
semantics.

There is no mid-flight rerouting in the MVP. A chunk whose remaining path becomes unavailable is
dropped with an explicit link-down reason. Packet mode, retransmission, and route repair are not
implicit fallbacks.

### Queue admission and service

Each directed fabric arc has an independent, non-preemptive, work-conserving FIFO serializer.
Opposite directions do not share queue capacity or service bandwidth.

The configured buffer capacity applies to chunks waiting behind the active serializer. The active
chunk is not counted as waiting-buffer occupancy. This distinction permits a coarse simulation
chunk to begin service on an idle link even when the chunk is larger than the waiting buffer.

Admission follows these rules:

1. reject the chunk if the directed arc is not operational;
2. if the serializer is idle and the waiting queue is empty, begin service immediately;
3. otherwise, atomically tail-drop the whole chunk if
   `waiting_bytes + chunk_bytes > buffer_capacity`;
4. otherwise, append the chunk to the FIFO and update waiting-byte and chunk counters;
5. mark an accepted waiting chunk when an enabled threshold is reached or exceeded after admission.

A mark is observable metadata and does not alter service in Cluster 3. Tail drop never admits
partial bytes. Multiple transfers are multiplexed only by deterministic FIFO arrival order.

When service completes, the active chunk leaves the serializer, the next waiting chunk begins
service immediately at the same simulated time, and the completed chunk begins propagation.
Propagation consumes no queue or serializer capacity.

### Integer timing

Serialization delay is:

```text
ceil(chunk_bytes * 8 * 1,000,000,000 / bits_per_second) nanoseconds
```

The implementation uses checked integer arithmetic and rejects values whose exact result cannot fit
in simulated time. Every valid nonzero chunk at finite nonzero bandwidth therefore consumes at
least one nanosecond.

For an idle one-hop link:

```text
completion_time =
    enqueue_time
    + serialization_delay
    + propagation_delay
```

For queued chunks, service start is the preceding serialization completion. Propagation overlaps
later serialization, preserving full pipeline behavior.

### Simulation events and deterministic ordering

Transport adds typed value payloads for chunk arrival and transmission completion. Payloads contain
stable IDs and hop position only; paths, queues, and transfer objects remain runtime-owned.
Every new payload receives an explicit stable trace kind, and the complete `Event` size is
remeasured against the Gate 1 budget.

Event priorities are:

- operational-state changes at `Critical`;
- transmission completions at `Control`;
- chunk arrivals at `Normal`.

At one timestamp, failures therefore reconcile before service completions, and service completions
free capacity before new arrivals. Equal-priority order remains the engine-assigned event-ID order.

### Failure and recovery

Simulation-facing link, port, and switch state changes go through the transport façade, which
updates `TopologyGraph` and reconciles affected directed runtimes in the same event handler.

When an arc becomes unavailable:

- its scheduled active-service completion is cancelled;
- its active and waiting chunks are dropped with a link-down reason at the failure time;
- waiting occupancy becomes zero and the serializer becomes idle;
- chunks that already completed serialization and are propagating are not recalled.

An in-flight chunk checks the next arc when it arrives. Recovery starts with an empty idle runtime
and does not retry dropped chunks.

### Progress and observability

Transfer progress records total, delivered, in-flight, waiting, active, and dropped bytes and chunk
counts. A transfer produces a successful completion only after every chunk reaches the destination
with zero drops. When every chunk is terminal and one or more dropped, it produces a failed outcome
with stable drop reasons. Other already-admitted chunks continue to terminal state so accounting
does not depend on cancellation reachability.

Per-direction runtime evidence includes:

- current and maximum waiting bytes and chunks;
- enqueued, service-started, serialization-completed, marked, and dropped bytes and chunks;
- serializer busy nanoseconds;
- drops by stable reason.

Busy time closes at actual service completion or failure time. At every transition, active,
waiting, propagating, delivered, and dropped chunk bytes must sum to the submitted transfer bytes.

Cluster 8 will own durable telemetry and time-series emission. Cluster 3 exposes deterministic
snapshots and counters without creating a second event log.

## Rationale

The separate runtime preserves the topology abstraction approved at Gate 2 while using its directed
identity exactly as intended. FIFO tail drop is the smallest queue model that makes congestion and
loss observable without embedding an optimization policy. Separating serialization from
propagation produces analytically checkable timing and permits realistic pipelining at chunk-level
fidelity.

Fixed per-chunk paths prevent Cluster 3 from preempting routing-policy work while leaving Cluster 4
a clean admission point. Explicit failure reconciliation uses the cancellation and stable-state
semantics already established by Clusters 1 and 2.

## Consequences

Positive:

- Idle-link and multi-hop completion times are analytically testable.
- Full-duplex directions have independent deterministic service.
- Finite waiting buffers expose queue buildup, marking, and tail drop.
- Topology, transport configuration, runtime state, and future telemetry remain separate.
- Later routing policies can choose chunk paths without redesigning link queues.
- Link failure has immediate, deterministic effects on active and queued work.

Negative:

- Chunk size affects head-of-line blocking, event count, and loss granularity.
- FIFO does not represent priority or fair queueing.
- Local GPU-to-NIC transfer time is omitted initially.
- Propagating chunks survive a later failure of the link they already left.
- There is no retransmission, route repair, ECN response, or packet protocol behavior.
- Two event transitions per hop can become a material event-volume cost.

## Validation

Cluster 3 must:

- compare idle one-hop and two-hop completion with exact analytical timing;
- test FIFO order, work conservation, full-duplex independence, and propagation overlap;
- test waiting occupancy, exact-capacity admission, overflow, marking, and stable drop reasons;
- test zero values, invalid paths, arithmetic overflow, and unknown IDs before mutation;
- test link failure during active and queued service, cancellation, recovery, and in-flight behavior;
- prove deterministic completion and counters across repeated identical runs;
- verify transfer progress never loses, duplicates, or produces negative bytes;
- measure the new event and payload sizes against the Gate 1 budget;
- benchmark one million chunk events, many-to-one incast, all-to-all traffic, and 100 and 10,000
  concurrent flows;
- publish chunk-size sensitivity for event count, runtime, queue depth, drops, and completion time.

Architecture Gate 3 must approve the transfer abstraction and its measured event volume before
routing or workload generation builds on it.

## Revisit Triggers

Revisit this decision if:

- chunk-size sensitivity changes a published policy conclusion;
- representative workloads require priority, fair, or non-FIFO service;
- event volume exceeds the established simulation-core guardrails;
- local attachment or shared bidirectional capacity becomes material;
- a validated scenario requires retransmission, mid-flight rerouting, or packet semantics;
- event payload growth exceeds the Gate 1 size budget;
- failure reconciliation cannot remain atomic through the transport façade.
