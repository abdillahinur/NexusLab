<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-005: Define the Topology and Cluster Model

## Status

Accepted — 2026-07-25

## Context

Cluster 2 must represent GPUs, NICs, switches, racks, ports, and links without coupling the graph to
future routing, queueing, workload, or placement implementations. The model must generate the
approved 512-GPU initial Clos target and 2,048-GPU stretch target, scale to the 8,192-GPU benchmark,
support failure state and routing queries, and export deterministic data for visualization.

The graph must not overfit the first Clos profile. Direct links, single-rack networks, leaf-spine
networks, and future topology generators must use the same entities and validation rules.

## Options Considered

### Identifier model

#### Strong ID type per entity kind

Benefits:

- Prevents accidental comparison or lookup across GPU, NIC, switch, port, rack, and link IDs.
- Keeps event payloads and future policy interfaces compact and explicit.
- Allows dense per-kind storage with constant-time lookup.

Costs:

- Generic tooling needs explicit conversion to a tagged entity reference.

#### One untyped integer ID

Benefits:

- Simple generic maps and serialization.

Costs:

- Permits cross-kind mistakes.
- Requires globally coordinated numbering for unrelated entity classes.

#### String identifiers

Benefits:

- Human-readable in configuration and logs.

Costs:

- Higher storage and comparison cost.
- Canonical formatting and lookup behavior require additional rules.

### Link directionality

#### One physical link with two derived directed arcs

Benefits:

- Models one full-duplex cable or attachment and one shared up/down state.
- Gives Cluster 3 one queue and service state per direction without duplicating physical identity.
- Makes whole-link failure and recovery atomic.

Costs:

- Directional state must use a link ID plus an explicit direction.

#### Two unrelated directed link entities

Benefits:

- Each direction is independently addressable.

Costs:

- Pairing, physical failure, validation, and visualization require extra conventions.

#### One undirected graph edge only

Benefits:

- Smallest topology representation.

Costs:

- Defers directionality until queue implementation and forces a graph redesign.

### Port representation

#### Explicit port entities

Benefits:

- Makes endpoint ownership, link occupancy, direction, and future per-direction queues explicit.
- Supports switch and link failure boundaries without inventing ports later.
- Allows visualization to identify exact attachment points.

Costs:

- Adds entities and validation work.

#### Connect devices directly

Benefits:

- Smaller Cluster 2 model.

Costs:

- Cannot validate port occupancy or model port-level state.
- Would require an incompatible topology change when queues and failures arrive.

### Physical and logical topology

#### Separate physical topology from job placement

Benefits:

- Physical entities and links remain stable across workload experiments.
- Logical ranks, collective roles, and scheduler placement can vary without graph mutation.

Costs:

- Later clusters need an explicit mapping from logical roles to GPU IDs.

#### Store logical placement in topology entities

Benefits:

- One object contains both connectivity and current use.

Costs:

- Couples Cluster 2 to workload and scheduler lifecycles.
- Makes topology reuse and comparative placement experiments harder.

## Decision

### Entity identity and storage

GPU, NIC, switch, rack, port, and physical-link entities each use a distinct strong ID backed by
`std::uint64_t`. IDs are assigned monotonically per kind in deterministic construction order.

Canonical topologies use dense zero-based IDs per kind. The graph stores each entity kind in
ID-indexed contiguous storage for constant-time lookup. A tagged entity reference is used only where
generic traversal or reporting must refer to more than one node kind.

IDs are stable value data. They do not contain addresses, container positions from unrelated entity
kinds, random values, or process-specific state.

### Entity model

- A GPU worker records its `GpuId`, owning rack, and attached NIC.
- A NIC records its `NicId`, owning rack, attached GPU IDs, and port IDs.
- A switch records its `SwitchId`, owning rack when applicable, role, and port IDs.
- Switch roles initially include leaf and spine; future roles can be added without changing the
  graph representation.
- A rack groups GPUs, NICs, and leaf switches but is not itself a routing vertex.
- A port records its `PortId`, owning connectable entity, role, and operational state.
- A physical link records its `LinkId`, two distinct port endpoints, link kind, and operational
  state.

GPU-to-NIC attachments and fabric connections are both explicit physical links, distinguished by
link kind. Cluster 3 may attach transmission, latency, buffer, and queue state only to the link kinds
that require it.

Bandwidth, propagation latency, buffers, queue disciplines, drops, and congestion state do not
belong to Cluster 2.

### Directionality and adjacency

Every physical link produces exactly two directed arcs:

```text
endpoint A -> endpoint B
endpoint B -> endpoint A
```

A directed arc is identified by the physical `LinkId` plus an explicit direction. Cluster 3 begins
with one queue and service state per directed fabric arc. Independent one-way physical failures are
deferred until a validated scenario requires them.

Node adjacency is derived from links and stored in deterministic directed-arc order. Behavioral
logic and serialization never depend on iteration order from unordered containers.

### Operational state and failures

Entity identity and graph structure are stable after construction. Ports, links, and switches expose
an explicit up/down operational state. Failure and recovery change that state; they do not delete
entities, renumber IDs, or rewrite adjacency.

Routing queries ignore arcs whose link, source port, destination port, or owning switch is down.
Cluster 9 will own failure scenarios and recovery timing, while Cluster 2 proves state transitions
and reachability changes.

### Generic topology graph

The topology graph owns entities, links, deterministic adjacency, and operational state. Topology
generators are separate builders that submit entities and links through validated construction APIs.

The same graph supports:

- a two-GPU direct topology;
- a single-rack topology;
- a configurable leaf-spine topology;
- the initial two-tier Clos profile;
- future generators that satisfy the same entity and validation rules.

Logical job ranks, collective roles, and scheduler placement are separate mappings to `GpuId` and are
deferred to Clusters 5 through 7.

### Initial topology families

The direct topology contains two racks, each with one GPU and one dedicated NIC, plus one
NIC-to-NIC fabric link and no switches. The direct GPU-to-GPU path is therefore three physical
hops.

The single-rack generator accepts a GPU count and GPUs per NIC. It requires exact divisibility,
creates one leaf switch, and connects every NIC in the rack to that leaf.

The configurable leaf-spine generator accepts leaf count, NICs per leaf, GPUs per NIC, and spine
count. Every leaf owns one rack and connects exactly once to every spine. The Clos generator derives
these dimensions from its GPU-count-oriented configuration and delegates construction to the same
leaf-spine builder, preserving one canonical graph layout and ID order.

### Initial Clos profile

Cluster 2 implements a deterministic two-tier leaf-spine Clos generator with:

- GPU count;
- GPUs per NIC;
- NICs per leaf;
- spine count.

Each GPU attaches to one NIC. Each NIC attaches to one leaf. Each leaf connects once to every spine.
One rack represents one leaf failure domain and contains that leaf, its NICs, and their GPUs. Spines
are fabric-wide and have no rack owner.

The generator rejects configurations where GPU count is not divisible by GPUs per NIC or NIC count
is not divisible by NICs per leaf. It does not silently create partially populated racks.

The approved reference profile uses eight GPUs per NIC, eight NICs per leaf, and eight spines:

| Scale | GPUs | NICs | Leaf switches and racks | Spine switches |
|---|---:|---:|---:|---:|
| Initial target | 512 | 64 | 8 | 8 |
| Stretch target | 2,048 | 256 | 32 | 8 |

For GPUs attached to different leaves, the known equal-cost leaf-to-leaf path count is the configured
spine count. Fat-tree may be accepted as a naming alias for a compatible generated profile, not as a
second graph implementation.

### Validation

Validation returns structured errors rather than a single Boolean. It checks:

- dense, unique IDs within each entity kind;
- valid rack and owner references;
- unique port ownership and at most one physical link per port;
- two distinct endpoints per link;
- no duplicate endpoint pair;
- endpoint and link-kind compatibility;
- exactly two directed arcs per physical link;
- deterministic adjacency order;
- required GPU-to-NIC and NIC-to-leaf attachment;
- graph connectivity for all active GPUs and NICs;
- Clos divisibility, counts, roles, and leaf-to-spine completeness;
- operational-state consistency.

Construction APIs reject local invariant violations immediately. Whole-graph validation reports all
independent structural errors that can be collected safely.

### Serialization and visualization

Human-authored and exported topology uses YAML through yaml-cpp. The canonical schema includes an
explicit schema version and ordered arrays for every entity kind and physical link. Serialization
sorts by strong ID and emits enum names defined by NexusLab; it never serializes addresses, padding,
unordered-container order, or derived adjacency.

Deserialization validates the schema version, reconstructs entities and links, rebuilds adjacency,
and runs whole-graph validation. A serialize-deserialize-serialize round trip must be byte-stable for
canonical output.

Graphviz DOT export is a derived visualization format, not a round-trip storage format. Protobuf and
durable replay remain deferred to Cluster 12.

## Rationale

Strong IDs and contiguous per-kind storage provide safe, efficient lookup while remaining compact
enough for event payloads. Explicit ports and two directed arcs per physical link establish the
minimum structure required by routing, queueing, failure injection, and visualization without
implementing Cluster 3 behavior early.

Separating the generic graph from the Clos generator prevents the initial deployment profile from
becoming the domain model. Separating physical connectivity from logical placement allows the same
fabric to support many workload and scheduling experiments.

## Consequences

Positive:

- Entity lookup is deterministic and constant time.
- Full-duplex directionality and future queue ownership are explicit.
- Link and switch failures preserve stable identity and adjacency.
- Clos, direct, and future topology builders share one validated graph.
- Clos and generic leaf-spine profiles share one canonical construction path.
- Canonical YAML supports inspection and visualization without introducing replay serialization.

Negative:

- Explicit ports and local GPU-to-NIC links increase entity and link counts.
- Dense IDs require canonical renumbering when importing noncanonical external descriptions.
- Whole-link failure is the only initial physical-link failure granularity.
- A two-tier Clos abstracts chassis, line cards, and deeper multistage fabrics.

## Validation

Cluster 2 must:

- test strong ID separation and deterministic assignment;
- test entity and link creation, lookup, and duplicate rejection;
- test physical links and both derived directions;
- test port occupancy and endpoint compatibility;
- test connectivity, shortest-hop distance, and known equal-cost path counts;
- test failure and recovery reachability without ID or adjacency mutation;
- generate direct, single-rack, leaf-spine, and Clos topologies;
- generate and validate 64, 512, 2,048, and 8,192 GPUs;
- round-trip canonical YAML and produce deterministic DOT output;
- benchmark construction, validation, shortest-path preprocessing, memory, and serialization size;
- run cleanly under GCC, Clang, ASan, UBSan, formatting, and clang-tidy checks.

## Revisit Trigger

Revisit this decision if:

- representative topologies require sparse or externally assigned IDs without canonicalization;
- port entities dominate memory at the 8,192-GPU scale;
- routing requires deeper Clos stages or parallel physical links not represented by the initial
  profile;
- a validated scenario requires independent one-way physical failure;
- one queue per directed fabric arc cannot represent a required link model; or
- YAML topology size or parse cost becomes unacceptable before Cluster 12.
