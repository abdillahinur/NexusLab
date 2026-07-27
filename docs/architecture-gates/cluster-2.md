<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 2

Date: 2026-07-27

## Correctness

- GPUs, NICs, switches, racks, ports, and physical links use distinct strong IDs with deterministic
  dense lookup.
- Validated construction owns all entities and creates two deterministic directed arcs for every
  full-duplex physical link.
- Direct, single-rack, leaf-spine, and Clos generators produce the same topology-neutral graph
  model.
- The approved Clos profile is deterministic at 512 GPUs and the 2,048-GPU stretch target; the same
  generator and validator also pass at 64 and 8,192 GPUs.
- Shortest-hop queries ignore failed links, ports, and switches and report known equal-cost path
  counts.
- Link, port, and switch failure and recovery preserve stable IDs, storage, and adjacency.
- Whole-topology and Clos-specific validation return structured errors for malformed identity,
  ownership, relationships, occupancy, links, adjacency, and connectivity.
- Canonical versioned YAML round-trips byte-for-byte, and deterministic Graphviz DOT export exposes
  topology and operational state for visualization.
- One hundred three CTest cases pass under GCC and Clang.
- AddressSanitizer and UndefinedBehaviorSanitizer pass all 103 tests.
- Linux clang-format and clang-tidy checks pass.

## Abstraction

- `TopologyGraph` is independent of Clos construction; topology families are generator clients of
  the same validated entity API.
- Explicit ports make ownership, link occupancy, direction, and failure state observable without
  embedding queue behavior in the topology.
- A physical link owns identity and whole-link operational state. Its two directed arcs are the
  boundary on which Cluster 3 can place independent queue and service state.
- Public read-only spans and strong-ID lookup avoid exposing storage ownership or permitting
  unvalidated graph mutation.
- Operational changes update state rather than deleting entities, so routing and visualization see
  stable identity across failure and recovery.
- Physical topology remains separate from future logical job placement, which can map workload
  ranks to stable `GpuId` values.
- Routing policy, queueing, transfer progress, workload placement, durable replay, and control
  interfaces remain deferred to their owning clusters.

## Performance and Memory

The release topology benchmark measures four deterministic Clos scales in three fresh processes on
the accepted WSL2 reference environment. The validator was optimized from repeated port-by-link
scans to one linear occupancy pass before this gate.

| GPUs | Construction | Validation | Path query | YAML serialization | Construction RSS delta | Peak RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 64 | 26,810 ns | 12,810 ns | 2,659 ns | 2,392,726 ns | 224 KiB | 3,928 KiB |
| 512 | 136,156 ns | 70,506 ns | 16,866 ns | 19,262,155 ns | 392 KiB | 4,680 KiB |
| 2,048 | 519,846 ns | 280,578 ns | 77,108 ns | 75,175,078 ns | 928 KiB | 7,132 KiB |
| 8,192 | 2,600,988 ns | 1,390,793 ns | 321,994 ns | 318,295,621 ns | 3,108 KiB | 16,968 KiB |

- The linear validator is 26.05 times faster at 2,048 GPUs and 87.46 times faster at 8,192 GPUs
  than the first measured implementation.
- The 2,048-GPU stretch topology remains the routine regression scale. The 8,192-GPU case is
  evidence of scaling headroom, not an expanded product target.
- Canonical YAML emission is now the dominant measured phase. It is accepted for human-authored
  configuration and visualization interchange; durable result serialization remains deferred to
  Protobuf in Cluster 12.
- Complete methodology, raw measurements, and before-and-after evidence are recorded in the
  [Cluster 2 topology baseline](../benchmarks/cluster-2-topology-baseline.md) and
  [validation optimization report](../benchmarks/cluster-2-validation-optimization.md).

## Initial Regression Guardrails

These are local engineering alerts, not hardware-neutral product claims or hard hosted-CI timing
checks. Evaluate the median of three fresh 2,048-GPU release processes on the documented WSL2
reference environment:

- construction must remain at or below 750,000 ns;
- generic plus Clos validation must remain at or below 400,000 ns;
- the representative GPU 0 to final-GPU path query must remain at or below 120,000 ns;
- canonical YAML serialization must remain at or below 100,000,000 ns;
- construction RSS delta must remain at or below 1,250 KiB;
- peak RSS must remain at or below 9,500 KiB.

The ceilings allow approximately 30% or more workstation variance over the optimized medians while
detecting structural regressions. The canonical 2,048-GPU profile must also remain exactly 864,019
YAML bytes unless an architecture gate explicitly accepts a schema or formatting change; that byte
count is a determinism contract, not a speed threshold.

The 64, 512, 2,048, and 8,192-GPU correctness matrix remains mandatory. Fresh 8,192-GPU benchmark
results must be recorded at architecture gates that materially change entity storage, graph
adjacency, validation, path queries, or serialization.

## Extensibility

- New topology families can use the validated construction API without changing graph consumers.
- Strong per-kind IDs can be carried by future routing, workload, telemetry, and replay records.
- Directed fabric arcs admit Cluster 3 queue and service state without changing physical-link
  identity.
- The versioned YAML schema provides an explicit compatibility boundary; unsupported versions and
  unknown enum values fail rather than being guessed.
- Deterministic dense adjacency supports current on-demand queries and leaves route caches or
  precomputation as measured future choices.
- The summary CLI can inspect approved generated profiles and canonical YAML without becoming a
  simulation or control interface.

## Failure Behavior

- Invalid Clos dimensions and arithmetic overflow fail before unsafe allocation.
- Duplicate or non-dense IDs, missing owners, incompatible endpoints, reused ports, parallel links,
  invalid rack membership, broken adjacency, and disconnected local attachments are rejected with
  structured validation errors.
- Invalid YAML syntax, missing fields, unsupported schema versions, unknown values, non-canonical
  identities, and invalid reconstructed relationships fail clearly.
- Queries reject unknown nodes and return no route when operational failures disconnect endpoints.
- Whole-link failure changes both directions atomically; port and switch state independently remove
  affected arcs from operational routing.
- Export validates the graph before emitting YAML or DOT, so malformed topology is not silently
  published.

## Checkpoint Answers

- **Does the model overfit Clos?** No. The graph owns general entities and adjacency, while direct,
  single-rack, leaf-spine, and Clos are separate generators using the same API.
- **Can future topologies be added cleanly?** Yes. A generator can compose validated entities and
  links without adding topology-specific behavior to `TopologyGraph`.
- **Are switch ports necessary at this stage?** Yes. They are the explicit ownership, occupancy,
  direction, failure, and future queue attachment boundary.
- **Is one queue per directed link sufficient?** It is the accepted initial Cluster 3 model: one
  queue and service state per directed fabric arc. Queue semantics and measurements are not part of
  Cluster 2.
- **Can logical job placement be represented separately?** Yes. Future placement maps logical ranks
  or tasks to stable GPU IDs without modifying physical topology.

## Accepted Risks and Deferrals

- Shortest-path queries use on-demand BFS with no path cache; Cluster 4 routing measurements will
  determine whether preprocessing is justified.
- Parallel physical links and deeper Clos stages are unsupported until a validated scenario
  requires them.
- Physical-link failure is initially whole-link; independent one-way failure is deferred until a
  scenario demonstrates the need.
- Bandwidth, propagation latency, buffering, queue discipline, and transfer state belong to
  Cluster 3.
- Routing policies belong to Cluster 4, failure scenario timing to Cluster 9, and durable replay
  serialization to Cluster 12.
- Canonical YAML is intentionally verbose at evidence scale and is not the future result-store
  format.
- The baseline is workstation-specific and cannot support public comparative performance claims.

## Evidence Recorded

- Topology-model decision: [ADR-005](../adr/ADR-005-topology-and-cluster-model.md).
- Initial measured revision: `153d86ddb2f53fb167c81d47ede1f9440f73dde1`.
- Optimized implementation revision: `8fa91ea87db917828a99f74f5d37b7de8ef56bba`.
- Baseline environment: Ubuntu 24.04.4 LTS on WSL2, AMD Ryzen 9 5900X, GCC 13.3.0.
- GCC and Clang builds with warnings as errors: passed.
- CTest under GCC and Clang: 103/103 passed.
- ASan/UBSan CTest: 103/103 passed.
- Linux clang-format check: passed.
- Linux clang-tidy build: passed.
- Canonical YAML sizes are stable at every required scale.
- Hosted GitHub Actions CI run 22 for revision
  `68316cb629596f03c31d8c662ae151a1a61dd031` reported successful on 2026-07-27.

## Decision

Proceed: **YES**

Required changes:

- None.

Cluster 2 is approved. Cluster 3 link, queue, and transfer work may begin under the master plan.
