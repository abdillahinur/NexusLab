<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab — Master Engineering Plan

> **Working title:** NexusLab  
> **Tagline:** The deterministic experimentation lab behind the NexusBench evaluation standard
> **Primary goal:** Build NexusLab as the counterfactual platform and NexusBench as the open,
> reproducible standard for comparing GPU-cluster infrastructure policies under frozen scenarios,
> explicit fidelity limits, and verifiable artifacts.

---

## 0. Document Purpose

This document is the source of truth for building NexusLab.

It is intended to:

- define the project vision and boundaries;
- prevent uncontrolled scope growth;
- divide the system into implementation clusters;
- force architecture decisions before code generation;
- provide checkpoints where development pauses for review;
- define testable acceptance criteria;
- document what Claude Code, Codex, or other coding agents may implement;
- preserve the reasoning behind major technical decisions;
- make the scientific method, engineering decisions, and limitations independently reviewable.

This is a living document. Any major change to architecture, scope, or interfaces must be reflected here before implementation continues.

---

# 1. Executive Summary

NexusLab is the open deterministic counterfactual experimentation lab for AI training
infrastructure. NexusBench is the reproducible benchmark/research standard built on that lab.

It models:

- GPUs and GPU workers;
- NICs, racks, switches, links, queues, and buffers;
- distributed training jobs;
- compute phases and communication phases;
- collective operations such as AllReduce;
- routing and congestion-control strategies;
- GPU placement and scheduling decisions;
- failures, degraded links, and bursty traffic;
- telemetry, metrics, experiment replay, and policy comparison.

NexusLab is not intended to perfectly reproduce every hardware-level detail of a real datacenter.

Its main purpose is to answer comparative engineering questions such as:

- Does queue-aware routing outperform ECMP for this workload?
- Does Ring or Tree AllReduce perform better under this topology?
- How much GPU idle time is caused by network congestion?
- What happens when a spine switch fails mid-training?
- Does topology-aware GPU placement reduce job completion time?
- Which policy remains stable under delayed or incomplete telemetry?
- Which controller improves p99 completion time without harming fairness?

The first release uses fully simulated data. NexusBench-v0 later freezes a synthetic routing and
placement subset; it does not claim calibrated hardware prediction.

Later versions may:

- import real GPU, NCCL, scheduler, and switch telemetry;
- replay real cluster incidents;
- operate in shadow mode beside a real cluster;
- produce advisory recommendations;
- integrate with Kubernetes, Slurm, NCCL configuration, or network controllers.

---

# 2. Project Positioning

## 2.1 One-Sentence Description

NexusLab is an open deterministic counterfactual experimentation lab for AI training infrastructure;
NexusBench is its reproducible benchmark standard for comparing scheduling, communication, routing,
congestion, and failure-recovery policies under frozen scenarios.

## 2.2 Thirty-Second Pitch

NexusLab creates a configurable virtual GPU cluster and runs synthetic or trace-derived distributed-
training workloads across it. NexusBench freezes scenario packs, metric contracts, repeats, policy
versions, and digests so other engineers can compare policy artifacts reproducibly. NexusLab records
telemetry and replay evidence explaining why jobs slowed down; later shadow/advisory work reuses the
same policy boundary without applying actions.

## 2.3 What NexusLab Is

NexusLab is:

- a discrete-event simulation engine;
- a GPU-cluster digital twin;
- a network-fabric simulator;
- a distributed-training workload simulator;
- a policy experimentation framework;
- a deterministic benchmark runner;
- a telemetry and replay platform;
- a future foundation for live-cluster advisory tooling.

NexusBench is the research program and shared evaluation standard built on these lab capabilities.

## 2.4 What NexusLab Is Not

NexusLab is not:

- a real GPU training framework;
- a replacement for PyTorch, NCCL, Slurm, or Kubernetes;
- a production-grade packet-level RDMA simulator in version one;
- a claim that synthetic results perfectly predict real hardware;
- an AI-generated dashboard with arbitrary numbers;
- a one-off visualization;
- a single routing algorithm demo;
- a tool that automatically changes production infrastructure without safety controls.

---

# 3. Why This Project Exists

## 3.1 Engineering Problem

Large distributed training jobs are affected by more than GPU compute speed.

Performance can degrade because of:

- poor GPU placement;
- oversubscribed links;
- incast;
- queue buildup;
- slow or failed workers;
- collective algorithm choice;
- route instability;
- checkpoint traffic;
- uneven link utilization;
- scheduler fragmentation;
- telemetry delay;
- switch or NIC failures.

Testing policy changes on a real cluster is expensive and risky.

NexusLab provides a controlled environment where the same workload can be rerun under many infrastructure configurations.

## 3.2 Research Goal

NexusLab and NexusBench should contribute:

- an open, frozen evaluation standard for comparable training-infrastructure policy research;
- deterministic, independently reproducible experiment artifacts;
- explicit topology, workload, failure, metric, and fidelity contracts;
- analytically validated simulation components and measured implementation limits;
- honest evidence, including neutral, adverse, failed, and disagreement cases;
- a practical path from synthetic comparison to trace counterfactuals, calibration, and non-applied
  advisory evaluation.

## 3.3 Scientific and Engineering Contribution

The completed project should provide independently reviewable evidence of:

- system ownership;
- architectural judgment;
- technical depth;
- performance analysis;
- extensibility;
- failure handling;
- test strategy;
- operational thinking;
- documentation quality;
- transparent provenance and review of AI-assisted engineering changes.

---

# 4. Success Criteria

NexusLab is successful when it can:

1. Generate a configurable Clos-style GPU cluster.
2. Run deterministic distributed-training simulations.
3. Model compute and communication phases.
4. Generate collective communication traffic.
5. Compare at least three routing policies.
6. Compare at least two collective strategies.
7. simulate congestion and failures;
8. measure job completion time, GPU idle time, queue depth, throughput, drops, and fairness;
9. rerun identical experiments from the same seed;
10. replay an experiment through a web dashboard;
11. explain major controller decisions;
12. execute benchmark suites automatically;
13. detect performance regressions in CI;
14. support additional policies without rewriting the simulator;
15. import a versioned recorded training-step trace and replay the same reconstructed work under
    alternate infrastructure policies;
16. model data, tensor, pipeline, and expert parallel communication in addition to Ring AllReduce;
17. represent intra-node high-bandwidth domains and rail-aware inter-node fabrics without claiming
    full RDMA fidelity;
18. publish one reproducible, measured A/B study with locked inputs, repeats, digests, and caveats;
19. run the same policy boundary against simulation, trace replay, and a mock shadow observation
    source while recording `applied=false` audit decisions;
20. publish NexusBench-v0 as a versioned, frozen, digest-verifiable benchmark standard with at
    least two baseline policies for every included track;
21. let an external policy author swap one conforming policy through the documented submission
    boundary and obtain comparable manifests, metrics, repeats, and digests;
22. document assumptions and limitations honestly.

The thin MVP remains the runnable first release defined in Section 24. The scientific release is
complete only when an external researcher can clone the repository, run synthetic and trace-driven
comparisons, inspect advanced parallelism over a heterogeneous multi-rail topology, reproduce the
flagship study, review shadow-mode decisions, and understand every fidelity boundary without
assistance. NexusLab is the deterministic counterfactual experimentation lab; NexusBench is the
open research standard built on it. A polished dashboard alone does not satisfy this criterion.

---

# 5. Guiding Principles

## 5.1 Determinism First

A simulation run with the same:

- configuration;
- seed;
- build version;
- policy version;

must produce identical output unless nondeterministic execution is explicitly enabled.

## 5.2 Comparative Accuracy Over Perfect Realism

The simulator must be useful for comparing policies.

NexusLab does not need to reproduce every hardware detail before it can answer:

> Under the same model and workload, does Policy A outperform Policy B?

## 5.3 Modular Policies

Routing, scheduling, congestion control, collective planning, and failure recovery must be replaceable through stable interfaces.

## 5.4 Explainable Decisions

A policy decision should be traceable to:

- the observed state;
- the policy version;
- the candidate actions;
- the selected action;
- the expected benefit;
- the reason for rejection of alternatives.

## 5.5 Measurable Everything

No claim of improvement is accepted without:

- a baseline;
- a controlled experiment;
- reproducible configuration;
- recorded metrics;
- documented assumptions.

## 5.6 Simulation and Production Boundaries

The controller must not depend directly on simulated internals.

It should interact through a backend abstraction that can later support:

- simulation;
- trace replay;
- shadow mode;
- real telemetry;
- advisory actions.

## 5.7 Small Vertical Slices

Each cluster should end with something runnable, testable, and reviewable.

## 5.8 Stop at Architecture Gates

Coding must pause at each architecture gate.

A coding agent must not continue into the next cluster until the review questions are answered.

---

# 6. Scope

## 6.1 Version 1 Scope

Version 1 includes:

- single-process discrete-event simulation;
- synthetic workloads;
- Clos topology;
- configurable links and queues;
- GPU workers;
- Ring AllReduce;
- multiple concurrent jobs;
- ECMP;
- least-loaded routing;
- queue-aware routing;
- link and switch failures;
- deterministic experiment runs;
- canonical JSON output, with Protobuf deferred until Cluster 12;
- CLI benchmark runner;
- replay artifacts and CLI inspection, with the web dashboard sequenced after scientific-release
  schemas are approved;
- core metrics and policy comparison.

This is the thin runnable product MVP. It does not, by itself, satisfy NexusBench research-ready or
scientific-release-complete status.

## 6.2 Scientific Release Scope

The scientific release follows the thin MVP and requires all of the following gated capabilities:

- NexusBench-v0 with frozen scenario packs, policy baselines, a metric contract, three-repeat
  matrices, deterministic digests, and reproducible artifact instructions;
- first-class data-parallel, tensor-parallel, pipeline-parallel, and expert-parallel traffic models;
- intra-node NVLink- or NVSwitch-like bandwidth domains plus rail-aware inter-node IB/RoCE-like
  topology, placement, routing, and failure hooks;
- import of a NexusLab-defined intermediate training-step trace and deterministic replay of the
  reconstructed workload under alternate routing, placement, and failure policies;
- a published, reproducible flagship A/B study with real run receipts and explicit synthetic or
  trace-derived labeling;
- a production-shaped backend boundary with simulation, trace replay, mock shadow, and advisory
  modes, plus a decision audit log proving that shadow actions were not applied.

Ring AllReduce, a single Clos fabric, and synthetic scenarios remain supported foundations. They are
not, by themselves, the completed scientific release. Cluster 24, Clusters 20–23, and expanded
Cluster 16 own this scope; their architecture gates cannot be waived for schedule or dashboard
polish.

## 6.3 Later Optional Scope

After the scientific release, later versions may include:

- Tree and hierarchical collective variants beyond the required DP/TP/PP/EP traffic patterns;
- ECN-style signals, delay-based congestion control, and flowlet routing;
- experiment databases, scenario editors, and runtime Python policy processes;
- real DCGM, Prometheus, Kubernetes, Slurm, switch, NCCL, or nsys source adapters;
- emulated workers, network namespaces, or multi-node simulator execution;
- human-approved real-cluster recommendations and a separately reviewed safety/rollback framework.
- a NexusBench calibration/validity track comparing simulated and real or semi-real policy
  rankings;
- multi-fidelity simulation selected through a future ADR when measured NexusBench matrix cost
  justifies it;
- hosted leaderboards after the repository-native result contract is trusted.

## 6.4 Explicit Non-Goals for Initial Release

Do not build these before the MVP is complete:

- full RDMA protocol simulation;
- bit-exact NCCL, Verbs, RoCE, InfiniBand, NVLink, or GPU execution reproduction;
- real switch programming;
- reinforcement learning controllers;
- distributed simulator execution;
- arbitrary user scripting;
- multi-cloud deployment;
- exact reproduction of proprietary GPU hardware;
- full browser-based scenario authoring;
- authentication;
- billing;
- SaaS multi-tenancy;
- mobile UI;
- Kubernetes operator;
- production auto-remediation.

NexusBench-v0 additionally does not require full RDMA/Verbs fidelity, a reinforcement-learning
controller, a multi-node distributed simulator, a hosted leaderboard, or production actuation.
Those additions cannot displace frozen scenarios, deterministic digests, baselines, and honest
comparative claims.

Imported traces are treated as approximate workload evidence, not proof of hardware-equivalent
timing or protocol behavior. Shadow mode in the scientific release is decide-but-do-not-apply; real
actuation, automatic remediation, and production scheduler integration require a future safety ADR
and are not implied by the backend interface.

---

# 7. Primary Users

## 7.1 Infrastructure Engineer

Wants to compare network or scheduling policies.

## 7.2 Researcher

Wants deterministic experiments and reproducible results.

## 7.3 Platform Engineer

Wants to understand how workload placement affects cluster performance.

## 7.4 Researcher or External Evaluator

Wants to inspect the architecture, replay scenarios, and understand distributed training behavior.

## 7.5 Future Real-Cluster Operator

Wants to import telemetry and ask what alternative policies might have done.

---

# 8. Core User Stories

## 8.1 Scenario Creation

As an engineer, I can define:

- cluster size;
- topology;
- link bandwidth;
- buffer size;
- workloads;
- policies;
- failures;
- random seed.

## 8.2 Policy Comparison

As an engineer, I can run the same scenario with multiple policies and compare results.

## 8.3 Deterministic Replay

As an engineer, I can replay the exact sequence of events from a completed experiment.

## 8.4 Root-Cause Analysis

As an engineer, I can inspect why a training job slowed down.

## 8.5 Failure Injection

As an engineer, I can inject failures at precise simulated times.

## 8.6 Benchmark Automation

As an engineer, I can run a matrix of scenarios and policies through one command.

## 8.7 Plugin Development

As an engineer, I can implement a new routing policy without editing the event engine.

## 8.8 Real Telemetry Import

As a future operator, I can translate real telemetry into NexusLab’s internal state model.

---

# 9. High-Level Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│ Scenario Definition                                         │
│ topology, jobs, policies, failures, seed, experiment matrix │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Experiment Orchestrator                                     │
│ validates config, expands matrix, assigns run IDs           │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Discrete-Event Simulation Core                              │
│ clock, event queue, deterministic ordering, lifecycle       │
└───────────────┬───────────────────────┬─────────────────────┘
                │                       │
                ▼                       ▼
┌────────────────────────┐   ┌───────────────────────────────┐
│ Cluster and Fabric     │   │ Training Workload Engine      │
│ GPUs, NICs, switches,  │   │ jobs, steps, compute,         │
│ links, queues, routes  │   │ collectives, gradient buckets │
└───────────────┬────────┘   └───────────────┬───────────────┘
                │                            │
                └──────────────┬─────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ Policy Layer                                                │
│ routing, congestion, scheduling, collective planning        │
└───────────────────────────┬─────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Telemetry and Metrics                                       │
│ events, snapshots, aggregates, decisions, anomalies         │
└───────────────────────────┬─────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Result Store and Replay                                     │
│ summary, event trace, metadata, policy versions             │
└───────────────────────────┬─────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ CLI, Reports, and Web Dashboard                             │
│ comparisons, timelines, topology view, decision inspector   │
└─────────────────────────────────────────────────────────────┘
```

---

# 10. Technology Stack

## 10.1 Simulation Core

- C++20
- CMake
- Catch2 or GoogleTest
- sanitizers
- clang-format
- clang-tidy
- optional benchmark library such as Google Benchmark

## 10.2 Configuration and Serialization

Recommended initial choice:

- YAML for human-written scenario files;
- protobuf for compact internal event and replay schema;
- JSON summaries for easy inspection;
- CSV export for analysis.

## 10.3 Backend Services

Possible stack:

- C++ simulation executable;
- small Node.js, Go, or Python orchestration API;
- WebSocket or Server-Sent Events for replay streaming;
- SQLite for MVP experiment metadata;
- PostgreSQL only if needed later.

The simulation core must remain usable without the web backend.

## 10.4 Frontend

- Next.js or React;
- TypeScript;
- Canvas or WebGL for topology rendering;
- charts for timelines and metrics;
- WebSocket client for replay;
- no dependency on live simulation for initial replay mode.

## 10.5 Analysis

- Python;
- pandas;
- matplotlib;
- Jupyter notebooks for offline experiment analysis.

## 10.6 CI/CD

- GitHub Actions;
- Linux builds;
- unit tests;
- sanitizers;
- deterministic regression tests;
- benchmark smoke tests;
- frontend lint and tests;
- artifact generation for benchmark summaries.

---

# 11. Repository Structure

```text
nexuslab/
├── README.md
├── NEXUSLAB_MASTER_PLAN.md
├── ARCHITECTURE.md
├── ROADMAP.md
├── CONTRIBUTING.md
├── LICENSE
├── CHANGELOG.md
├── CMakeLists.txt
├── cmake/
├── docs/
│   ├── concepts/
│   ├── design/
│   ├── adr/
│   ├── benchmarks/
│   ├── experiments/
│   ├── diagrams/
│   └── research-notes/
├── schemas/
│   ├── scenario/
│   ├── telemetry/
│   ├── replay/
│   └── results/
├── simulator/
│   ├── include/nexuslab/
│   │   ├── core/
│   │   ├── topology/
│   │   ├── fabric/
│   │   ├── workload/
│   │   ├── policies/
│   │   ├── telemetry/
│   │   ├── experiment/
│   │   └── replay/
│   ├── src/
│   ├── tests/
│   └── benchmarks/
├── policies/
│   ├── routing/
│   ├── congestion/
│   ├── scheduling/
│   └── collective/
├── scenarios/
│   ├── smoke/
│   ├── baseline/
│   ├── congestion/
│   ├── failures/
│   ├── multi-tenant/
│   └── scale/
├── services/
│   ├── experiment-api/
│   └── replay-server/
├── web/
│   ├── app/
│   ├── components/
│   ├── lib/
│   └── tests/
├── analysis/
│   ├── notebooks/
│   ├── scripts/
│   └── reports/
├── scripts/
│   ├── build.sh
│   ├── test.sh
│   ├── run_scenario.sh
│   ├── run_matrix.sh
│   └── generate_report.sh
├── examples/
└── .github/workflows/
```

---

# 12. Domain Model

## 12.1 Core Entities

### Simulation

Owns:

- current simulated time;
- event queue;
- run state;
- random generator;
- metrics registry;
- event dispatcher.

### Event

Contains:

- event type;
- simulated timestamp;
- deterministic sequence number;
- source entity;
- destination entity;
- payload;
- causation ID;
- correlation ID.

### GPU Worker

Contains:

- worker ID;
- job assignment;
- compute state;
- communication state;
- GPU utilization;
- waiting reason;
- local NIC references.

### NIC

Contains:

- link references;
- send queues;
- receive queues;
- bandwidth;
- health state;
- transmission counters.

### Switch

Contains:

- switch ID;
- switch type;
- ports;
- forwarding state;
- queue state;
- failure state.

### Link

Contains:

- source;
- destination;
- bandwidth;
- propagation delay;
- queue;
- buffer capacity;
- utilization;
- health state;
- drop policy.

### Queue

Contains:

- queued bytes;
- queued items;
- enqueue time;
- service rate;
- mark or drop thresholds.

### Flow

Contains:

- flow ID;
- job ID;
- collective ID;
- source and destination;
- bytes total;
- bytes sent;
- route;
- start time;
- completion time;
- priority;
- flowlet state.

### Training Job

Contains:

- job ID;
- model profile;
- worker count;
- step count;
- arrival time;
- priority;
- assigned GPUs;
- state;
- compute and communication statistics.

### Training Step

Contains:

- compute phase;
- gradient bucket readiness;
- collective operations;
- barrier conditions;
- completion state.

### Collective Operation

Contains:

- collective type;
- participants;
- algorithm;
- message size;
- chunk size;
- channels;
- dependencies;
- completion criteria.

### Policy Decision

Contains:

- policy name and version;
- decision type;
- input state hash;
- candidate actions;
- selected action;
- reason;
- expected impact;
- confidence;
- actual observed impact.

---

# 13. Time and Event Model

## 13.1 Simulated Time

Recommended representation:

```cpp
using SimTimeNs = std::uint64_t;
```

Requirements:

- no floating-point timestamps in core scheduling;
- explicit conversion helpers;
- overflow checks;
- deterministic ordering for identical timestamps.

## 13.2 Event Ordering

Events should be ordered by:

1. timestamp;
2. priority class;
3. deterministic sequence number.

Questions to resolve:

- Which event classes require priority?
- Should failure events execute before transmission-completion events at the same timestamp?
- How are simultaneous arrivals handled?
- Can policy evaluation schedule events at the current timestamp?

## 13.3 Event Types

Initial event types:

- job arrival;
- job scheduled;
- compute start;
- compute complete;
- collective start;
- flow created;
- packet or chunk enqueue;
- transmission start;
- transmission complete;
- dequeue;
- flow complete;
- collective complete;
- training step complete;
- link failure;
- link recovery;
- switch failure;
- telemetry sample;
- policy evaluation;
- policy action applied;
- simulation stop.

## 13.4 Simulation Lifecycle

```text
load config
validate config
construct topology
construct workloads
initialize policies
schedule initial events
run event loop
record telemetry
finalize metrics
write result
write replay
```

---

# 14. Configuration Model

## 14.1 Example Scenario

```yaml
schema_version: 1

scenario:
  name: 512-gpu-multi-tenant-failure
  seed: 12345
  duration_limit_ms: 300000

topology:
  type: clos
  racks: 16
  gpus_per_rack: 32
  leaf_switches_per_rack: 1
  spine_switches: 8
  link_bandwidth_gbps: 400
  propagation_delay_ns: 500
  queue_capacity_mb: 32

workloads:
  - id: job-a
    arrival_ms: 0
    workers: 256
    steps: 200
    compute_ms_per_step: 35
    gradient_mb: 900
    collective:
      type: allreduce
      algorithm: ring

  - id: job-b
    arrival_ms: 250
    workers: 128
    steps: 150
    compute_ms_per_step: 25
    gradient_mb: 500
    collective:
      type: allreduce
      algorithm: ring

policies:
  routing: queue_aware
  congestion_control: none
  scheduling: first_fit
  collective_planner: fixed

failures:
  - at_ms: 10000
    type: link_down
    target: spine-3:port-4
    duration_ms: 3000

telemetry:
  sample_interval_us: 100
  record_event_trace: true
  record_queue_snapshots: true
```

## 14.2 Validation Rules

The validator should reject:

- duplicate IDs;
- disconnected topologies;
- impossible worker counts;
- zero bandwidth;
- negative or zero durations;
- unsupported policies;
- failures targeting missing entities;
- incompatible schema versions;
- invalid collective participant counts;
- experiments expected to exceed configured safety limits.

---

# 15. Metrics

## 15.1 Job Metrics

- job completion time;
- p50, p95, p99 completion time;
- queueing delay before scheduling;
- time spent computing;
- time spent communicating;
- time spent blocked;
- step duration;
- straggler delay;
- GPU idle time;
- slowdown relative to isolated execution.

## 15.2 Network Metrics

- link utilization;
- bytes transmitted;
- queue occupancy;
- queueing delay;
- drops;
- marks;
- reroutes;
- route changes;
- path stretch;
- packet or chunk reordering;
- goodput;
- throughput;
- congestion episode duration.

## 15.3 Scheduler Metrics

- allocation delay;
- fragmentation;
- cross-rack placement;
- locality score;
- utilization;
- preemption count;
- fairness.

## 15.4 Controller Metrics

- decisions made;
- decisions accepted;
- actions applied;
- action latency;
- stale-state decisions;
- oscillation count;
- confidence;
- predicted versus actual benefit.

For the Cluster 16 scientific-release boundary, `actions applied` must remain zero. Shadow/advisory metrics
distinguish decisions, would-apply actions, stale observations, rejected capabilities, and the
constant `applied=false` audit result.

## 15.5 Reliability Metrics

- failure detection time;
- recovery time;
- affected jobs;
- dropped traffic;
- convergence time;
- SLO violations.

## 15.6 Fairness Metrics

Potential measures:

- Jain’s fairness index;
- per-job slowdown ratio;
- priority-weighted fairness;
- starvation count.

## 15.7 Scientific-Release Metrics

- pipeline bubble time and stage idle time;
- collective wait and pipeline wait without interval double counting;
- communication-caused GPU idle time by parallel group;
- DP, TP, PP, and EP logical/fabric bytes;
- per-rail utilization, fallback, and dropped traffic;
- cross-rail bytes and high-bandwidth-domain locality;
- imported-workload digest equality across comparison cells;
- shadow would-apply count, stale-observation decisions, and applied count fixed at zero.

---

# 16. Experiment Methodology

## 16.1 Controlled Comparison

Every comparison must hold constant:

- topology;
- workload arrivals;
- job profiles;
- random seed;
- failure timing;
- simulation version;
- metric definitions.

Trace-driven comparisons also hold the canonical imported-workload digest, importer/schema versions,
and reconstruction settings constant. Infrastructure policies may differ only where the comparison
manifest explicitly declares an override.

Only the policy under evaluation should change.

## 16.2 Repeated Runs

For stochastic scenarios:

- use a fixed seed set;
- report mean and variance;
- preserve every seed;
- avoid selecting only favorable runs.

The flagship study uses at least three repeats per cell unless its published protocol documents an
evidence-based reason for another count. Every repeat remains in the manifest, including failed,
neutral, or adverse outcomes.

## 16.3 Baselines

Required baselines:

- ECMP routing;
- first-fit scheduling;
- fixed collective selection;
- no congestion controller.

## 16.4 Benchmark Matrix

Example:

```text
5 topology sizes
× 4 workload profiles
× 4 routing policies
× 3 failure scenarios
× 10 seeds
= 2,400 runs
```

## 16.5 Claims Policy

Do not say:

> NexusLab improved training performance by 20%.

Say:

> Under the documented synthetic workload model, the queue-aware policy reduced simulated p99 job completion time by 20% relative to ECMP across the selected benchmark set.

For trace-driven results, say “under the documented approximate trace-derived workload model” and
identify the locked workload digest. Never shorten this into a claim about NCCL, RDMA, or real-cluster
speedup.

---

# 16A. NexusBench Research Program

## 16A.1 Research Thesis and Problem

NexusLab is the lab: an open, deterministic counterfactual experimentation platform. NexusBench is
the research: a reproducible open benchmark and experimentation standard for GPU-cluster routing,
placement, collective, congestion-control, and failure-recovery policies.

Training-infrastructure policy papers and tools are difficult to compare when they use different
topologies, workloads, seeds, metric definitions, failure timings, simulator versions, and fidelity
assumptions. NexusBench addresses that comparability gap with frozen scenario packs, metric and
repeat contracts, policy boundaries, version constraints, and content digests. Trace ingest,
DP/TP/PP/EP models, heterogeneous multi-rail topology, attribution, calibration, and shadow mode
exist to strengthen NexusBench tracks and the adoption path, not as unrelated product features.

## 16A.2 What NexusBench Is

NexusBench provides:

- versioned suites such as NexusBench-v0 and NexusBench-v1;
- frozen scenarios that lock topology profile, synthetic or trace-driven workload definitions,
  parallelism pattern, failure script, seed set, simulator compatibility, and metric definitions;
- a documented policy submission interface whose policies consume bounded public views and do not
  depend on simulator internals;
- a scoring contract that distinguishes primary and secondary metrics and records repeats, input
  and output digests, confidence fields, failed cells, and caveats;
- a repository-native leaderboard/results format for Markdown summaries and CI artifacts before
  any hosted leaderboard exists;
- citation and reproducibility requirements that identify the NexusBench suite version, NexusLab
  version/commit, scenario pack, policy version, seed set, result digest, and limitations.

The planning-only specification outline is
[docs/nexusbench/README.md](docs/nexusbench/README.md). It must not be presented as an implemented
suite until Cluster 24 passes.

## 16A.3 What NexusBench Is Not

NexusBench is not:

- a claim of perfect hardware or predictive fidelity;
- a replacement for NCCL, PyTorch, Slurm, Kubernetes, or production observability systems;
- a requirement to build a packet-accurate RDMA/Verbs simulator;
- an auto-remediation controller or production switch-programming path;
- a vanity dashboard or a collection of favorable one-off results.

## 16A.4 Planned Tracks

| Track | Question | Supporting clusters | Earliest status |
|---|---|---|---|
| NB-Routing | How do routing policies compare under incast, all-to-all, and spine failure? | 4, 8–9, 11–12, 24 | NexusBench-v0 |
| NB-Placement | How do placement policies compare under rack pressure and multi-tenant interference? | 7–8, 11–12, 24 | NexusBench-v0 |
| NB-Collective | How do policies respond to DP, TP-heavy, PP-bubble, and EP all-to-all patterns? | 20, 24 | Planned v1 candidate |
| NB-Recovery | How do policies behave on failed or degraded fabrics? | 9, 21, 24 | Planned after two baselines exist |
| NB-Trace | What changes when the same ingested trace is replayed under alternate policies? | 22, 24 | Planned v1 candidate |
| NB-Validity | Do simulated policy rankings correlate with real or semi-real measurements? | 14, 24 | Later calibration extension |

Congestion-control policies may enter applicable routing/recovery scenarios after Cluster 10
provides at least two gate-approved baselines; reinforcement learning is an optional later entrant,
not a v0 requirement.

Multi-rail identity from Cluster 21 extends routing, placement, collective, and recovery tracks with
rail-aware scenarios. Cluster 14 provides stall/root-cause attribution and counterfactual deltas as
an analysis layer over runs; it is not a competing research identity. Multi-fidelity simulation is
a later performance response if measured matrix cost demands it and is not a NexusBench-v0 blocker.

## 16A.5 NexusBench-v0 Frozen Scope and Acceptance Bar

NexusBench-v0 is deliberately synthetic-only and includes two tracks:

- **NB-Routing-v0:** locked incast and spine-link-failure scenarios comparing ECMP and queue-aware
  routing while topology, workload, placement, failures, seeds, and metrics remain fixed;
- **NB-Placement-v0:** locked rack-pressure and multi-tenant scenarios comparing first-fit and
  compact placement while topology, arrivals, routing, seeds, and metrics remain fixed.

NexusBench-v0 is accepted only when:

- the frozen scenario pack is checked into the repository and its version cannot be changed without
  an explicit suite-version decision;
- identical scenario, seed, policy version, simulator version, and metric catalog produce identical
  canonical input and outcome digests;
- every included track has at least two documented baseline policies;
- one deterministic harness runs a three-repeat matrix for every required comparison cell;
- the metric dictionary names every primary/secondary metric, unit, aggregation, missing-data rule,
  and caveat field;
- a “how to add a policy” guide defines the submission contract, versioning, isolation limits, test
  obligations, and expected result package;
- Markdown and CI artifact outputs conform to the versioned leaderboard/results schema;
- every result is explicitly labeled synthetic, and documentation contains no fabricated values or
  placeholder claims.

The three-repeat rule is a reproducibility harness contract, not permission to claim statistical
significance. More repeats and confidence methods require a study-specific protocol.

## 16A.6 Flagship Reference Study

Cluster 23 is the first NexusBench case study and reference result. It uses a locked NexusBench
scenario/study manifest and the convention `docs/studies/<study-id>.md`, including a reproducible
command block, run IDs, digests, metric references, environment notes, raw-artifact locations, and
caveats. No study file receives numbers or completion language until the referenced measured runs
exist and verification passes. The artifact rules and planning template are in
[docs/studies/README.md](docs/studies/README.md).

## 16A.7 Research Claims

After the NexusBench-v0 gate and supporting evidence, the allowed claim is:

> Open reproducible benchmark for comparing training-infrastructure policies under locked scenarios.

Until an approved calibration study exists, NexusLab and NexusBench must not claim that they
predict real-cluster performance, match NCCL/RDMA reality, or are hyperscaler-proven. All public
claims must name whether the evidence is synthetic, trace-derived, mock-shadow, semi-real, or real.

---

# 17. Implementation Clusters

The project is divided into clusters. Each cluster must include:

- objective;
- questions;
- deliverables;
- interfaces;
- implementation tasks;
- tests;
- benchmark;
- documentation;
- checkpoint;
- architecture gate.

---

# Cluster 0 — Project Foundation

## Objective

Create a clean, reproducible engineering environment before implementing simulation behavior.

## Questions

- Which operating systems are supported?
- GCC, Clang, or both?
- Catch2 or GoogleTest?
- YAML library choice?
- protobuf in MVP or later?
- mono-repo or split repositories?
- how strict should warnings be?
- what code-coverage target is realistic?

## Deliverables

- repository skeleton;
- CMake build;
- test runner;
- formatter;
- linter;
- sanitizer configuration;
- CI workflow;
- contribution guide;
- issue templates;
- pull-request template;
- changelog;
- initial architecture documentation.

## Tasks

- configure `-Wall -Wextra -Wpedantic`;
- treat selected warnings as errors;
- configure ASan and UBSan;
- add clang-format;
- add clang-tidy;
- add unit-test executable;
- add benchmark executable;
- add `scripts/build.sh`;
- add `scripts/test.sh`;
- add CI matrix.

## Acceptance Criteria

- clean clone builds with one documented command;
- tests run in CI;
- sanitizers run in CI;
- formatting check fails on violations;
- a sample executable runs successfully.

## Checkpoint Questions

- Can a new contributor build NexusLab in under ten minutes?
- Are dependency versions pinned?
- Are build modes documented?
- Are debug and release builds separate?
- Is CI fast enough for every pull request?

## Architecture Gate 0

Do not proceed until the project can be built, tested, linted, and documented consistently.

---

# Cluster 1 — Deterministic Simulation Core

## Objective

Implement a high-performance discrete-event engine with deterministic replay behavior.

## Questions

- What is the timestamp unit?
- What is the event ownership model?
- Are event payloads variants, inheritance, or typed structs?
- How are equal timestamps ordered?
- Can events cancel other events?
- How is simulation stopped?
- How are random values generated?
- How are state snapshots created?
- Should the event queue use `std::priority_queue` initially?
- When is a custom heap justified?

## Deliverables

- simulation clock;
- event type;
- event queue;
- sequence number generator;
- event dispatcher;
- simulation lifecycle;
- deterministic RNG wrapper;
- event cancellation or invalidation strategy;
- simulation result status;
- basic trace logging.

## Interfaces

```cpp
class Simulation {
public:
    void schedule(Event event);
    void run();
    void stop(StopReason reason);
    SimTimeNs now() const;
};

struct Event {
    SimTimeNs timestamp;
    EventPriority priority;
    std::uint64_t sequence;
    EventPayload payload;
};
```

## Tests

- ordering by timestamp;
- ordering by priority;
- ordering by sequence;
- event scheduling during callbacks;
- empty queue behavior;
- stop conditions;
- deterministic seed behavior;
- invalid timestamp rejection;
- cancellation behavior;
- ten identical runs produce identical hashes.

## Benchmark

- one million no-op events;
- ten million no-op events;
- insertion throughput;
- dispatch throughput;
- memory usage;
- event size.

## Acceptance Criteria

- one million events complete within the agreed local threshold;
- deterministic state hash is stable;
- no sanitizer issues;
- no floating-point time in the event queue;
- benchmark results are recorded.

## Checkpoint Questions

- Does the simulator need cancellation now?
- Are event payloads too large?
- Is the current queue implementation sufficient?
- Is determinism enforced or merely expected?
- Can every event be traced to its cause?

## Architecture Gate 1

Review event ownership, deterministic ordering, memory behavior, and benchmark results before topology work begins.

---

# Cluster 2 — Topology and Cluster Model

## Objective

Represent GPUs, NICs, switches, racks, links, and configurable cluster topologies.

## Questions

- Which topology is MVP?
- How are bidirectional links represented?
- Should ports be explicit entities?
- Are queues owned by links or ports?
- How are entity IDs generated?
- How is adjacency stored?
- How is topology validation performed?
- How do failures change graph state?
- Should physical and logical topology be separate?

## Deliverables

- entity ID system;
- GPU worker model;
- NIC model;
- switch model;
- port model;
- link model;
- rack model;
- topology graph;
- Clos generator;
- topology validator;
- topology serializer;
- topology summary CLI.

## Initial Topologies

- two-node direct link;
- single rack;
- leaf-spine;
- Clos;
- optional fat-tree naming compatibility.

## Tests

- node and link creation;
- duplicate ID rejection;
- link directionality;
- graph connectivity;
- known path counts;
- topology size validation;
- Clos generation for several sizes;
- failure and recovery state;
- serialization round trip.

## Benchmark

Generate and validate:

- 64 GPUs;
- 512 GPUs;
- 2,048 GPUs;
- 8,192 GPUs.

Measure:

- construction time;
- memory use;
- shortest-path preprocessing time;
- serialization size.

## Acceptance Criteria

- topology generation is deterministic;
- invalid topologies fail clearly;
- the graph supports routing queries;
- entity lookup is efficient;
- topology can be exported for visualization.

## Checkpoint Questions

- Does the model overfit Clos?
- Can future topologies be added cleanly?
- Are switch ports necessary at this stage?
- Is one queue per directed link sufficient?
- Can logical job placement be represented separately?

## Architecture Gate 2

Do not proceed until the topology can support routing, failure injection, visualization, and scale testing without redesign.

---

# Cluster 3 — Link, Queue, and Transfer Model

**Status: Complete — 2026-09-05.** [Architecture Gate 3](docs/architecture-gates/cluster-3.md)
records the acceptance checklist, test results, performance evidence, and scope boundaries.

## Objective

Model data movement, service rate, buffering, latency, and congestion.

## Questions

- Packet-level, chunk-level, or flow-level simulation?
- What is the minimum useful transfer unit?
- How are link transmission and propagation delay separated?
- What queue discipline is MVP?
- How are drops handled?
- Is ECN marking simulated?
- How are multiple flows multiplexed?
- Is link service work-conserving?
- How are route changes handled mid-flow?

## Recommended MVP Decision

Use chunk-level simulation rather than individual network packets.

A chunk may represent:

- a gradient bucket segment;
- a fixed transfer quantum;
- a collective message fragment.

This provides meaningful queue behavior without requiring billions of packet events.

## Deliverables

- transfer chunk;
- link queue;
- enqueue and dequeue events;
- serialization delay;
- propagation delay;
- buffer capacity;
- tail-drop behavior;
- optional mark threshold;
- flow progress tracking;
- transfer completion.

## Tests

- single chunk on idle link;
- multiple chunks on one link;
- exact serialization delay;
- queue buildup;
- buffer overflow;
- drops;
- two-link path;
- link failure during queued transfer;
- zero-size transfer rejection;
- deterministic completion order.

## Benchmark

- one million chunk events;
- many-to-one incast;
- all-to-all traffic;
- 100 concurrent flows;
- 10,000 concurrent flows.

## Acceptance Criteria

- isolated transfer time matches analytical expectation;
- queue occupancy is correct;
- no negative bytes or time;
- congestion is observable through metrics;
- chunk size is configurable.

## Checkpoint Questions

- Is chunk-level fidelity enough for the project’s claims?
- Does the model produce stable behavior?
- Are queue semantics clear?
- Is the event count manageable?
- Should packet mode exist later behind another backend?

## Architecture Gate 3

Approve the transfer abstraction before routing and workload generation are built on top of it.

---

# Cluster 4 — Routing Policy Framework

**Status: Complete — 2026-09-05.** [Architecture Gate 4](docs/architecture-gates/cluster-4.md)
records correctness, routing comparisons, cache/snapshot costs, and accepted limitations.

## Objective

Create a pluggable routing layer and implement trustworthy baselines.

## Questions

- Per-flow, per-chunk, or flowlet routing?
- When is a route selected?
- How is ECMP hashing made deterministic?
- What state can a routing policy observe?
- Can routing policies mutate simulation state?
- How are loops prevented?
- How are failed links excluded?
- Should policy execution time be measured?

## Deliverables

- route representation;
- path enumeration;
- shortest-path service;
- routing-policy interface;
- ECMP;
- static shortest path;
- least-loaded path;
- queue-aware path;
- route-decision telemetry;
- policy registry.

## Accepted Interface Refinement

[ADR-007](docs/adr/ADR-007-routing-policy-boundary.md) refines the illustrative interface into
`RouteRequest` (flow key, endpoints, bytes and chunk size), `PolicyInput` (request, seed, borrowed
candidate paths and read-only fabric view), and `RoutingPolicy::choose` (validated candidate index,
integer score and reason). `Router` owns path lookup, submission and bounded `RouteDecision`
records. An uncalibrated floating-point confidence field is omitted. Selection is per transfer at
admission; current queue state is read synchronously. Failure rerouting applies to new admissions,
while existing dropped chunks retain the explicit no-retry transport semantics.

## Tests

- known shortest paths;
- ECMP stability;
- failed-link exclusion;
- no routing loops;
- queue-aware preference;
- tie-breaking;
- disconnected destination handling;
- policy registry;
- identical request produces identical decision.

## Benchmark

- path lookup latency;
- route selection latency;
- scale by topology size;
- route cache effectiveness;
- memory use of precomputed paths.

## Acceptance Criteria

- routing policies are swappable by configuration;
- simulator core does not reference specific policy classes;
- decisions are recorded;
- baseline policies are correct on known graphs;
- failures trigger valid rerouting.

## Checkpoint Questions

- Is the snapshot too expensive to copy?
- Should policies receive views instead of owning data?
- Is queue-aware routing using current or delayed telemetry?
- Does least-loaded routing oscillate?
- Is flowlet routing needed before MVP?

## Architecture Gate 4

Review policy boundaries, observability, and snapshot cost before adding advanced controllers.

---

# Cluster 5 — Training Workload Engine

**Status: Complete — 2026-09-05.** [Gate 5](docs/architecture-gates/cluster-5.md) approves
the synthetic lifecycle, explicit assignments, overlap model, job metrics and scenario schema.

## Objective

Generate realistic-enough distributed-training behavior from synthetic job profiles.

## Questions

- What defines a training step?
- How is compute time modeled?
- How are gradient buckets released?
- When can communication overlap with compute?
- Which collective operations are required?
- How are stragglers modeled?
- Are workers synchronized at step boundaries?
- How is checkpoint traffic represented?
- How are job arrivals generated?

## MVP Workload Model

A job contains:

- number of workers;
- number of steps;
- compute duration per step;
- gradient size;
- collective type;
- collective algorithm;
- job priority;
- arrival time.

MVP behavior:

```text
compute
→ allreduce
→ step complete
→ repeat
```

## Version 2 Behavior

```text
compute layer groups
→ gradient bucket ready
→ asynchronous bucket allreduce
→ overlap communication with remaining compute
→ step barrier
```

This completed overlap and bucket foundation remains valid. Cluster 20 extends it with explicit
parallel groups, pipeline stages, expert dispatch, and attribution; the bursty mixture-of-experts
profile here is only an approximation and does not satisfy the expert-parallel research gate.

## Deliverables

- job model;
- job lifecycle;
- worker assignment;
- compute events;
- training step;
- collective request;
- synthetic workload profiles;
- multi-job arrivals;
- job completion metrics.

## Synthetic Profiles

- small data-parallel training;
- large LLM training;
- communication-heavy workload;
- compute-heavy workload;
- bursty mixture-of-experts approximation;
- checkpoint-heavy workload;
- inference-style burst traffic.

## Tests

- single worker job;
- two-worker job;
- expected isolated completion time;
- multiple jobs;
- staggered arrivals;
- worker synchronization;
- straggler worker;
- cancelled or failed worker;
- deterministic workload generation.

## Acceptance Criteria

- isolated workload timing can be analytically verified;
- jobs produce network traffic through collective operations;
- job state transitions are explicit;
- multiple jobs can overlap;
- workload profiles are configuration-driven.

## Checkpoint Questions

- Is the training model understandable?
- Are workload assumptions documented?
- Does the model represent the critical path?
- Is compute/communication overlap needed now?
- Are synthetic profiles clearly labeled as approximations?

## Architecture Gate 5

Confirm that the workload abstraction reflects job completion and GPU idle time, not merely network flows.

---

# Cluster 6 — Collective Communication Engine

**Status: Complete — 2026-09-05.** [Gate 6](docs/architecture-gates/cluster-6.md) approves
the round-barrier Ring planner, local/fabric execution and workload integration.

## Objective

Translate distributed-training collective operations into network transfers.

## Questions

- Which collective is implemented first?
- How is Ring AllReduce decomposed?
- How are chunks scheduled?
- How are channels represented?
- How are participants ordered?
- How is completion detected?
- Can collective algorithms choose topology-aware routes?
- How is Tree AllReduce added later?

## MVP

Implement Ring AllReduce with:

- reduce-scatter phase;
- all-gather phase;
- configurable chunk size;
- deterministic participant order.

## Later Algorithms

- Tree AllReduce;
- hierarchical AllReduce;
- AllGather;
- ReduceScatter;
- AllToAll approximation.

Ring AllReduce remains the completed initial collective. Required scientific-release support for DP, TP, PP,
and EP is owned by Cluster 20 and must reuse this planner/executor separation rather than changing
the approved Cluster 6 history.

## Deliverables

- collective interface;
- Ring planner;
- collective phase state;
- chunk dependencies;
- completion tracking;
- per-collective metrics;
- collective timeline events.

## Tests

- byte accounting;
- phase transitions;
- participant coverage;
- completion conditions;
- two-worker ring;
- four-worker ring;
- failed participant behavior;
- known analytical transfer volume.

## Benchmark

- collective planning time;
- event count by worker count;
- memory use;
- completion time under ideal links;
- completion time under congestion.

## Acceptance Criteria

- total bytes match expected algorithm behavior;
- collective completion is deterministic;
- job cannot advance before collective completion;
- failures produce explicit outcomes;
- collective metrics can be visualized.

## Checkpoint Questions

- Are chunks too small or too large?
- Is participant ordering topology-aware?
- Is the abstraction general enough for Tree?
- Does the collective engine own routing?
- Should the collective planner and network policy remain separate?

## Architecture Gate 6

Approve separation of collective planning, routing, and transfer execution.

---

# Cluster 7 — Scheduler and GPU Placement

**Status: Complete — 2026-09-05.** [Gate 7](docs/architecture-gates/cluster-7.md) approves
non-preemptive scheduling, four placement policies, persistent GPU health and waiting/locality metrics.

## Objective

Assign jobs to GPU workers and measure the impact of placement.

## Questions

- Is scheduling centralized?
- Are jobs scheduled only on arrival?
- Is preemption supported?
- What is a valid GPU allocation?
- How is topology locality scored?
- How is fragmentation measured?
- How are priorities handled?
- How are failed GPUs excluded?

## MVP Policies

- first fit;
- random deterministic placement;
- rack-local preference;
- topology-aware compact placement.

## Deliverables

- cluster resource inventory;
- scheduler interface;
- allocation request;
- placement decision;
- job waiting queue;
- GPU release;
- locality metric;
- fragmentation metric;
- scheduler decision trace.

## Interface

```cpp
class SchedulingPolicy {
public:
    virtual PlacementDecision place(
        const JobRequest& job,
        const ClusterResourceView& resources
    ) = 0;
};
```

## Tests

- exact fit;
- insufficient capacity;
- placement across racks;
- failed GPU exclusion;
- job release;
- deterministic tie-breaking;
- priority ordering;
- fragmentation.

## Benchmark

Compare policies on:

- sequential arrivals;
- burst arrivals;
- mixed job sizes;
- partial failures;
- rack-local capacity pressure.

## Acceptance Criteria

- scheduling is independent of routing;
- placement decisions are explainable;
- waiting time is measured;
- jobs release resources correctly;
- locality affects generated communication paths.

## Checkpoint Questions

- Does the scheduler need reservations?
- Is preemption necessary?
- Is compact placement always beneficial?
- How are competing goals represented?
- Should scheduling policies use predicted network cost?

## Architecture Gate 7

Review scheduler scope and avoid adding production scheduler complexity before core comparisons work.

---

# Cluster 8 — Telemetry and Observability

## Objective

Make every important state transition and bottleneck measurable.

## Dependencies

- completed Clusters 1–7 domain events, snapshots, timelines, and policy decisions;
- ADR-011 accepted before implementation.

## In Scope

- versioned metric catalog, deterministic observations, summaries, traces, correlations, sampling,
  retention limits, and telemetry-overhead measurement;
- stable extension points for NexusBench metrics, advanced parallelism, multi-rail dimensions,
  imported provenance, and shadow audit references.

## Out of Scope

- durable result packaging and replay schemas owned by Cluster 12;
- dashboard rendering, live production telemetry, or claims of automatic root-cause certainty;
- changing simulation decisions or event ordering based on telemetry mode.

## Questions

- Event-based telemetry, sampled telemetry, or both?
- What is always recorded?
- What can be disabled for performance?
- How are high-volume metrics aggregated?
- How are decision records correlated?
- How large can replay files become?
- What is the retention strategy?
- How are metric definitions versioned?

## Deliverables

- metrics registry;
- counters;
- gauges;
- histograms;
- time-series samples;
- event trace;
- decision trace;
- correlation IDs;
- summary builder;
- telemetry configuration;
- telemetry overhead benchmark.

## Required Observability

Every job slowdown should be attributable to one or more:

- scheduling wait;
- compute;
- communication;
- queueing;
- dropped or retried traffic;
- failed resource;
- policy action;
- synchronization barrier;
- straggler.

Cluster 20 extends the catalog with pipeline bubble time, collective- and pipeline-wait idle time,
and parallel-group traffic. Cluster 21 adds rail and high-bandwidth-domain dimensions plus
cross-rail traffic. Cluster 8 must leave those additions versionable without pretending they are
already implemented.

## Tests

- counter correctness;
- histogram boundaries;
- event correlation;
- metric reset between runs;
- disabled telemetry;
- serialization;
- trace version compatibility;
- no missing completion metrics.

## Benchmark

Measure simulation overhead with:

- telemetry disabled;
- summary metrics only;
- sampled telemetry;
- full trace.

## Acceptance Criteria

- summaries match raw events;
- metric names and units are documented;
- replay contains enough information for the UI;
- telemetry can be reduced for scale tests;
- policy decisions are inspectable;
- the NexusBench metric dictionary can lock names, units, aggregations, missing-data behavior, and
  catalog versions without depending on dashboard code.

## Checkpoint Questions

- Are we recording too much?
- Can summaries be reproduced from traces?
- Which metrics are essential for the NexusBench research artifacts?
- Are units explicit?
- Are timestamps aligned across subsystems?

## Architecture Gate 8

Approve telemetry schemas before building replay and dashboard features. The gate must answer yes or
no: are metric names/units/versioning stable; does full-trace rebuild equal the live summary; is
telemetry observational; are retention failures explicit; and can NexusBench freeze a metric catalog
without depending on UI code?

## Planned Demo and NexusBench Link

Run one incast scenario in summary and full-trace modes, prove canonical summary equivalence, and
inspect the metric catalog. This unlocks the metric foundation for every NexusBench track and
Adoption Stage 0; it does not establish a benchmark result on its own.

---

# Cluster 9 — Failure Injection and Recovery

## Objective

Model predictable failures and measure policy response.

## Dependencies

- Clusters 2–4 topology, transport, routing, and operational revisions;
- Cluster 8 failure observations and metric catalog.

## In Scope

- deterministic failure scripts, detection, invalidation, recovery, affected-flow handling, and
  failure/recovery metrics for the declared components;
- reusable failure identity and ordering for frozen NexusBench scenarios.

## Out of Scope

- vendor-specific fault injection, live hardware disruption, or production remediation;
- exact link/NIC firmware behavior;
- rail/domain failure identity before Cluster 21 and ADR-015.

## Questions

- Which failures are MVP?
- How are failures scheduled?
- What happens to in-flight chunks?
- How are failed paths invalidated?
- How does the system detect failure?
- Is detection immediate or delayed?
- Can telemetry become stale?
- How does recovery occur?

## MVP Failures

- link down;
- link degraded;
- switch down;
- switch recovery;
- GPU worker failure;
- traffic burst;
- background checkpoint load.

## Later Failures

- silent packet loss;
- telemetry delay;
- stale controller state;
- partial NIC degradation;
- rack outage;
- synchronized job arrival;
- controller timeout.

Rail loss, NIC loss, and NVLink-domain degradation are promoted from generic future failure ideas
into required Cluster 21 scenarios. Cluster 9 provides the deterministic failure lifecycle and
ordering that those heterogeneous-fabric failures must reuse.

## Deliverables

- failure scenario schema;
- failure events;
- resource health states;
- failure detector abstraction;
- recovery events;
- affected-flow handling;
- recovery metrics;
- failure timeline.

## Tests

- failure before traffic;
- failure during transfer;
- recovery;
- path invalidation;
- reroute;
- unreachable destination;
- failed worker;
- repeated failure;
- deterministic failure ordering.

## Acceptance Criteria

- failures produce explicit state changes;
- metrics distinguish detection and recovery;
- invalid routes are not used;
- simulation ends gracefully when recovery is impossible;
- dashboard can display failure timelines;
- versioned failure scripts produce deterministic event ordering and digests suitable for frozen
  NexusBench scenarios.

## Checkpoint Questions

- Are failures observed immediately?
- Is delayed detection necessary for meaningful controller evaluation?
- How are in-flight bytes handled?
- Is retry modeled?
- What behavior is documented as simplified?

## Architecture Gate 9

Review failure semantics before comparing recovery policies. The gate must answer yes or no: are
failure timing/detection/recovery explicit; are in-flight bytes and unreachable outcomes defined;
are scripts deterministic and digestible; and can frozen scenarios compare policies without hidden
failure differences?

## Planned Demo and NexusBench Link

Run the same locked spine-link failure against ECMP and queue-aware routing and inspect failure,
reroute, and recovery timestamps. This supplies the failure dimension for NB-Routing-v0 and later
NB-Recovery; the latter remains closed until it has at least two genuine recovery baselines.

---

# Cluster 10 — Congestion-Control Framework

## Objective

Model sender-rate decisions separately from routing.

## Questions

- Is congestion control required for MVP or version 2?
- What signals are available?
- How is sender rate represented?
- How often can rates change?
- Is RTT modeled?
- How are fairness and convergence measured?
- How are oscillations detected?

## Policies

- no control;
- fixed rate;
- simplified ECN response;
- delay-based controller;
- telemetry-aware hybrid controller.

## Deliverables

- congestion-control interface;
- flow rate state;
- feedback events;
- mark handling;
- delay measurement;
- controller actions;
- convergence metrics.

## Tests

- single flow;
- two competing flows;
- ECN threshold response;
- rate floor and ceiling;
- fairness;
- convergence;
- delayed feedback;
- controller stability.

## Acceptance Criteria

- congestion control is independent of routing;
- rate changes affect link service;
- controller decisions are recorded;
- fairness and throughput are measured;
- unstable policies are detectable.

## Architecture Gate 10

Do not add sophisticated controllers until baseline behavior is analytically understood.

---

# Cluster 11 — Experiment Orchestrator

## Objective

Run single scenarios and large experiment matrices reproducibly.

## Dependencies

- gate-approved Cluster 8 metrics and Cluster 9 failure scripts;
- stable policy identities from Clusters 4 and 7 and later Cluster 15.

## In Scope

- deterministic matrix expansion, run identity, seeds, provenance, resumption, bounded local
  parallelism, partial-failure accounting, and study/NexusBench manifests.

## Out of Scope

- multi-node distributed orchestration, hosted submissions, production scheduling, or hiding failed
  cells;
- analysis/scoring logic owned by Clusters 14 and 24.

## Questions

- Is orchestration a CLI process or service?
- How are runs identified?
- How are policy versions captured?
- How are failures retried?
- Can runs execute in parallel?
- How are resource limits enforced?
- How are partial matrices continued after interruption?

## Deliverables

- scenario validator;
- experiment configuration;
- matrix expansion;
- run ID generation;
- seed generation;
- subprocess execution;
- parallel worker pool;
- progress reporting;
- result manifest;
- interrupted-matrix continuation support;
- failure reporting.
- locked study manifests containing topology, workload or imported-trace digest, seed, simulator
  version, metric-catalog version, and policy names/versions;
- deterministic three-repeat or explicitly justified repeat matrices for NexusBench studies.

## CLI Examples

```bash
nexuslab run scenarios/baseline/512-gpu.yaml
nexuslab compare scenarios/baseline/512-gpu.yaml \
  --routing ecmp,least_loaded,queue_aware
nexuslab matrix experiments/routing-suite.yaml
nexuslab replay results/run-12345
```

## Tests

- matrix expansion;
- deterministic run IDs;
- failed run capture;
- interrupted-run continuation behavior;
- duplicate prevention;
- seed generation;
- result manifest correctness.

## Acceptance Criteria

- one command runs a comparison suite;
- every run records full provenance;
- identical matrix definitions produce identical run IDs and input digests;
- interrupted matrices can continue without rerunning verified cells;
- failures do not corrupt successful runs;
- results are easy to locate.
- a frozen NexusBench matrix expands to the declared tracks, scenarios, policies, seeds, and exactly
  three required v0 repeats without implicit cells or favorable filtering.

## Architecture Gate 11

Review whether orchestration belongs in C++ or a separate service before scaling run management,
and answer whether matrix expansion, partial failure, resumption, policy identity, and seed handling
preserve the NexusBench contract.

## Planned Demo and NexusBench Link

Expand and continue an interrupted small routing matrix, then show stable run IDs and complete failed/successful
cell accounting. This supplies the repeat harness for NexusBench-v0 and deterministic policy CI at
Adoption Stage 0.

---

# Cluster 12 — Result Store and Replay Format

## Objective

Persist experiment summaries and event traces for later analysis and visualization.

## Dependencies

- Cluster 8 telemetry schemas and catalog versions;
- Cluster 11 manifests, run IDs, and matrix lifecycle.

## In Scope

- self-contained versioned run packages, canonical serialization, component digests, integrity
  checks, replay, compatibility, compression, and incomplete/failed-run state;
- NexusBench receipts and trace-import provenance extension points.

## Out of Scope

- live streaming, cluster control, hosted storage, or dashboard rendering;
- interpreting source-specific traces or collapsing multiple digest domains into one opaque hash.

## Questions

- JSON, protobuf, Arrow, or combination?
- What is the canonical replay format?
- How are schemas versioned?
- Are traces compressed?
- Can traces be streamed?
- How are large runs indexed?
- How are old schemas migrated?

## Deliverables

- run manifest;
- summary schema;
- replay schema;
- decision schema;
- compression strategy;
- reader and writer;
- schema version;
- integrity checksum;
- replay index.
- stable imported-trace references and content digests in run manifests;
- schema-extension points used by Cluster 22 for trace provenance without storing source-specific
  NCCL or nsys fields in the core replay model.

## Example Result Layout

```text
results/run-12345/
├── manifest.json
├── summary.json
├── metrics.csv
├── decisions.jsonl
├── replay.pb.zst
├── topology.json
├── scenario.yaml
└── logs.txt
```

## Tests

- round trip;
- corrupted file detection;
- schema mismatch;
- compression;
- partial trace;
- large trace streaming;
- compatibility test.

## Acceptance Criteria

- results are self-contained;
- replay does not require rerunning the simulation;
- provenance is preserved;
- schemas are documented;
- file sizes are measured.
- trace-derived runs remain self-contained and identify the importer/schema versions that produced
  their reconstructed workload.
- NexusBench result packages distinguish suite/scenario, policy artifact/configuration, simulator,
  metric-catalog, and outcome digests and detect corruption of each locked component.

## Architecture Gate 12

Approve replay format before the frontend depends on it, including explicit answers on canonical
serialization, digest scope, schema compatibility, incomplete runs, and NexusBench package
self-containment.

## Planned Demo and NexusBench Link

Persist one complete and one failed matrix cell, verify component digests, and replay the complete
cell without rerunning simulation. This supplies NexusBench-v0 receipts and Adoption Stage 0 policy-
CI evidence; it does not make the result hardware-calibrated.

---

# Cluster 13 — Web Replay Dashboard

**Scientific sequencing:** Cluster 13 follows Cluster 24, Clusters 20–23, the initial Cluster 16
shadow boundary, and the required Cluster 14 attribution path. It consumes their approved schemas
and evidence; dashboard polish must not delay or stand in for NexusBench-v0, trace replay, shadow
auditability, or measured research artifacts.

## Objective

Build a polished interface that explains cluster behavior and policy differences.

## Dependencies

- approved Cluster 12 replay/result schemas;
- gate-approved Cluster 24 NexusBench result contract, Clusters 20–23 evidence schemas, Cluster 16
  audit schema, and Cluster 14 attribution outputs.

## In Scope

- replay-only visualization of completed NexusBench comparisons, study receipts, attribution,
  parallelism, rails, traces, failures, metrics, and non-applied shadow decisions.

## Out of Scope

- live telemetry streaming, policy execution, result editing, benchmark scoring authority, cluster
  control, auto-remediation, or scenario-pack mutation.

## Main Views

### Overview

Shows:

- scenario metadata;
- policy configuration;
- key metrics;
- result status;
- baseline comparison.

### Fabric View

Shows:

- racks;
- switches;
- links;
- link utilization;
- queue occupancy;
- failed components;
- active flows;
- selected job or collective.

### Training Timeline

Shows:

- job arrivals;
- scheduling wait;
- compute phases;
- collective phases;
- stalls;
- failures;
- completion.

### Decision Inspector

Shows:

- selected decision;
- policy;
- observed state;
- candidates;
- chosen action;
- reason;
- predicted impact;
- actual outcome.

### Comparison View

Shows:

- policy summaries;
- completion-time differences;
- fairness;
- drops;
- GPU idle time;
- failure recovery.

### Replay Controls

Includes:

- play;
- pause;
- speed;
- seek;
- step event;
- filter by job;
- filter by entity;
- jump to failure;
- jump to congestion episode.

## Questions

- Canvas, SVG, or WebGL?
- Is topology replay streamed or preloaded?
- How large can a replay be?
- What information must remain visible?
- How are thousands of GPUs aggregated?
- How are colors made accessible?
- What is the smallest useful viewport?

## Deliverables

- dashboard shell;
- experiment list;
- replay loader;
- time controller;
- topology renderer;
- job timeline;
- metric cards;
- decision inspector;
- comparison page;
- responsive behavior;
- error states.

## Tests

- replay loading;
- seek behavior;
- filter behavior;
- large topology rendering;
- missing trace;
- schema mismatch;
- accessibility checks;
- visual regression tests.

## Acceptance Criteria

- a reviewer understands a scenario without reading source code;
- congestion and failures are visually obvious;
- the UI remains responsive on target replay size;
- every major metric has a unit and definition;
- decisions can be inspected;
- NexusBench views display suite/scenario and policy digests, synthetic/trace/mock labels, failed or
  missing cells, and source receipts without recomputing canonical results in the browser.

## Architecture Gate 13

Review whether the UI explains the system or merely decorates it. The gate must answer yes or no:
does the UI consume rather than redefine NexusBench contracts; are labels/limitations prominent;
are failed/missing cells visible; and is every chart traceable to immutable result artifacts?

## Planned Demo and NexusBench Link

Open a completed NexusBench comparison, verify its digests and labels, inspect one attribution chain,
and replay the associated failure or shadow decision. This improves explanation across all tracks
and adoption stages but unlocks no research or adoption stage by itself.

---

# Cluster 14 — Analysis and Reporting

## Objective

Generate trustworthy NexusBench reports, stall/root-cause attribution, counterfactual deltas, and
later calibration evidence without turning correlation into a claim of absolute timing accuracy.

## Dependencies

- Cluster 8 metric catalog and causal correlations;
- Clusters 11–12 manifests, digests, and saved results;
- Cluster 24 NexusBench scoring and result contracts;
- Cluster 22 for trace-driven attribution;
- ADR-018 before implementing the later calibration/validity protocol.

## In Scope

- reproducible aggregation, confidence/caveat fields, and regression reporting;
- attribution of scheduling wait, compute, communication, queueing, failures, policy actions,
  collective wait, and pipeline bubbles where the source schemas support them;
- paired counterfactual deltas between manifest-compatible NexusBench cells;
- the Cluster 23 reference-study report and repository-native NexusBench result summaries;
- a later held-out protocol comparing policy rankings with Spearman, Kendall, or an approved
  equivalent and explicitly reporting failure cases.

## Out of Scope

- causal inference beyond the modeled event graph and declared experiment intervention;
- hiding neutral, adverse, failed, or missing runs;
- claiming absolute simulator-to-hardware timing accuracy from ranking correlation;
- exposing private partner traces or metadata contrary to license, NDA, or anonymization rules.

## Deliverables

- Python result loader;
- comparison scripts;
- confidence intervals;
- seed aggregation;
- regression detection;
- benchmark report template;
- Markdown report generation;
- plots;
- experiment notebook examples.

## Required Reports

- routing policy comparison;
- scheduler comparison;
- failure recovery comparison;
- scale test;
- telemetry overhead;
- simulation-core benchmark;
- sensitivity to chunk size;
- sensitivity to queue capacity.
- the required Cluster 23 NexusBench reference study under `docs/studies/`, generated only from completed
  saved runs and never populated with placeholder measurements.

## Acceptance Criteria

- reports are reproducible from saved results;
- every chart links to scenario definitions;
- no manual spreadsheet editing is required;
- negative or neutral results are preserved;
- claims include limitations.
- the flagship study can be regenerated from its locked manifest and links every claim to run IDs,
  digests, and metric definitions.
- counterfactual deltas reject cells whose locked manifest dimensions are not comparable;
- the later validity report publishes rank-correlation method, held-out split, uncertainty, and
  disagreement/failure cases without revealing protected source data.

## Architecture Gate 14

The review must answer yes or no:

- Can every aggregate, attribution, and delta be traced to immutable runs and metric definitions?
- Are incompatible cells rejected instead of silently compared?
- Are missing, failed, neutral, and adverse outcomes preserved?
- Does the attribution layer distinguish measured event causality from interpretation?
- For calibration, are rankings compared on held-out scenarios and are correlation failures shown?
- Are private-trace schema, anonymization, license, and nondisclosure constraints respected?
- Do all claims stop short of absolute hardware prediction unless separate evidence supports it?

Proceed only when every answer is yes or an exception is documented and approved.

## Planned Demo

```bash
nexuslab report nexusbench results/nexusbench-v0 --track routing
nexuslab analyze delta results/studies/ep-incast-spine-ab --baseline ecmp
```

The demo regenerates a result table and follows one job slowdown through attributed wait intervals
and a paired policy delta. A later Stage 3 demo adds a held-out ranking-correlation report.

## NexusBench and Adoption Link

This cluster is the analysis layer for every NexusBench track. Its reproducible report path is
required for research-ready status; its held-out ranking protocol is the trust unlock for Adoption
Stage 3, not a NexusBench-v0 blocker.

---

# Cluster 15 — Plugin and Extension System

## Objective

Define the NexusBench policy submission boundary so new policies can be evaluated without modifying
the simulation core or depending on simulator-owned internals.

## Dependencies

- approved policy boundaries from Clusters 4, 6, 7, and later 10;
- Cluster 11 policy-version provenance;
- ADR-013 approved before the NexusBench submission SDK is implemented.

## Questions

- Compile-time C++ plugins or runtime shared libraries?
- Is a Python policy API worth the complexity?
- How are plugin versions recorded?
- How are unsafe plugins isolated?
- Which state is exposed?
- How is backward compatibility managed?

## MVP

Use statically registered C++20 policy implementations with bounded read-only inputs, deterministic
tie-breaking, stable metadata, and no direct event-engine or mutable topology access.

## Later

Consider:

- shared-library plugins;
- Python policy process via gRPC;
- sandboxed policy runner.

## In Scope

- a small versioned C++ policy SDK over public immutable views;
- registry metadata, capability declarations, configuration schemas, and policy digests;
- conformance tests for determinism, bounded execution inputs, invalid outputs, and result provenance;
- a sample out-of-tree-shaped policy submission that builds in the monorepo without core edits;
- a “how to add a policy” guide used by NexusBench-v0.

## Out of Scope

- untrusted native shared-library loading or process sandboxing in v0;
- Python/runtime network policy execution;
- access to internal queues, clocks, RNG objects, or mutable simulator state;
- production deployment or actuation of submitted policies.

## Deliverables

- policy registry;
- policy metadata;
- versioning;
- configuration binding;
- sample custom policy;
- extension guide.

## Acceptance Criteria

- a new policy can be added with minimal core changes;
- policy version is recorded in results;
- unsupported configuration fails clearly;
- sample plugin is documented;
- one policy swap changes only declared policy metadata/configuration while the frozen scenario and
  seed digests remain fixed;
- conformance tests reject nondeterministic metadata, invalid candidate selection, internal-type
  dependencies, and missing policy versions.

## Architecture Gate 15

The review must answer yes or no:

- Can a policy be added without modifying simulator-core behavior?
- Does the SDK expose only bounded, immutable, domain-level state?
- Are version, configuration, and artifact digests recorded in every result?
- Do conformance tests detect invalid outputs and determinism violations?
- Is the v0 boundary build-time/static, with unsafe runtime loading explicitly deferred?
- Can the same policy artifact later consume Cluster 16 backend-neutral observations?

Proceed only when every answer is yes or an exception is documented and approved.

## Planned Demo

```bash
nexuslab policy validate examples/policies/sample_queue_policy
nexuslab nexusbench run v0 --track routing --policy sample_queue_policy --repeats 3
```

The demo swaps a conforming policy into a frozen routing matrix and produces a comparable result
package without edits to the simulator core or scenario pack.

## NexusBench and Adoption Link

This cluster unlocks policy submission for every NexusBench track and supplies deterministic policy
artifacts for policy CI. Reusing the artifact at Cluster 16 supports Adoption Stage 2.

---

# Cluster 16 — Real Telemetry and Shadow Mode

## Objective

Define and demonstrate a production-shaped controller boundary in which the same policy code path
can evaluate simulator state, imported trace replay, or mock live observations without depending on
simulation internals. The scientific-release implementation stops at decide-but-do-not-apply shadow mode and
human-readable advisory output; it proves interface discipline and auditability, not production
control.

## Dependencies

- Cluster 8 typed telemetry, correlations, and terminal status;
- Cluster 9 failure observations;
- Clusters 11–12 run provenance and replay schemas;
- Clusters 15 and 24 policy submission, scenario, and digest contracts;
- Cluster 22 imported-trace intermediate schema and replay adapter;
- ADR-017 approved before implementation.

## Modes

### Simulation Mode

All state is generated internally.

### Replay Mode

State is reconstructed from an approved NexusLab replay or imported-trace run with provenance and
version digests.

### Shadow Mode

The mock live adapter feeds ordered recorded or generated cluster observations. NexusLab generates
decisions through the same policy path used by simulation and replay, records the action it would
take, and always persists `applied=false`.

### Advisory Mode

NexusLab formats a shadow decision for human review. It does not expose an actuation channel.

### Controlled Mode

Controlled mode is a future concept only. It is outside the scientific release and cannot be enabled
by configuration, adapter capability, or test fixture.

## In Scope

- backend-neutral observations, policy inputs, candidates, decisions, and audit records;
- simulation, trace-replay, mock-live shadow, and advisory adapters;
- a capability model that reports read-only behavior and rejects apply requests;
- deterministic observation ordering, stale/late observation handling, and versioned policy input;
- decision audit chain: observed state → candidates → chosen action → `would_apply` →
  `applied=false`;
- documented allowlists, dry-run behavior, rate limits, and human-approval hooks that remain
  non-actuating in this phase;
- advisory mapping of recommendations to placement constraints/affinities, routing choices or
  weights, collective configuration hints, and abstract scheduler-preference objects;
- tests proving one policy implementation runs unchanged in simulation, replay, and shadow.

## Out of Scope

- real switch or NIC programming;
- Kubernetes or Slurm operators;
- production DCGM, Prometheus, NCCL, nsys, or switch connections;
- automatic remediation, rollback execution, canaries, or closed-loop control;
- claims that mock shadow decisions are safe or effective on a real cluster.

## Public Interfaces and Schemas to Design Before Coding

```cpp
class ObservationBackend {
public:
    virtual std::optional<ClusterObservation> next_observation() = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual ~ObservationBackend() = default;
};

class ControllerPolicy {
public:
    virtual PolicyDecision decide(
        const ClusterObservation& observation,
        std::span<const CandidateAction> candidates
    ) = 0;
};
```

The pre-code design must define:

- `ClusterObservation` schema, source timestamp, ingest sequence, freshness, and provenance digest;
- `CandidateAction`, `PolicyDecision`, policy version, rejection reasons, and stable identifiers;
- `BackendCapabilities` with read-only/shadow/advisory flags and no scientific-release apply method;
- `DecisionAuditRecord` containing observation digest, candidates, chosen action, `would_apply`,
  `applied=false`, and reason;
- adapters from simulator state, NexusLab replay, and mock observation streams into the same
  observation contract;
- schema compatibility, bounded buffering, late/stale input behavior, and audit-log integrity.

## Safety Rules

- the scientific-release backend contract exposes observations and capabilities, not a real `apply` method;
- every shadow/advisory record sets `applied=false` in code and serialized output;
- mock inputs and generated observations are labeled at ingest, CLI output, audit output, and docs;
- unsupported capabilities, malformed/stale input, audit-write failure, or buffer exhaustion fail
  closed without producing an apparently applied action;
- no production credentials, controller endpoints, cluster-admin dependencies, or hidden network
  calls are permitted;
- adding actuation requires a separate future safety ADR, explicit user authorization, allowlists,
  human approval, rate limits, rollback/canary design, and independent validation.

## Potential Adapters

- simulated backend;
- replay backend;
- mock live adapter backed by recorded or generated observations;
- future-only DCGM, Prometheus, NCCL/nsys, Kubernetes, Slurm, and switch adapters.

## Acceptance Criteria

- one unchanged policy implementation produces schema-valid decisions in simulation, trace replay,
  and mock shadow mode for equivalent observations;
- mock shadow replay is deterministic for identical observation stream, policy version, and seed;
- every shadow/advisory decision records observed-state digest, ordered candidates, chosen action,
  reason, `would_apply`, and `applied=false`;
- advisory outputs are schema-valid operator-facing recommendations and cannot be submitted to a
  production endpoint;
- apply attempts and adapters claiming write capability fail closed;
- bounded buffers reject overflow explicitly and stale/late observations follow documented rules;
- audit output round-trips through the Cluster 12 schema and detects corruption/version mismatch;
- CLI and docs repeatedly label the adapter mock and the decision non-applied;
- no production credentials, endpoints, or control dependencies are required.

## Architecture Gate 16

The review must answer yes or no:

- Is the policy interface independent of simulator-owned types and clocks?
- Does the same policy binary path run in simulation, replay, and mock shadow modes?
- Is every action path incapable of applying a real change in this release?
- Are observation freshness, ordering, capability negotiation, and failure behavior explicit?
- Can every decision be reconstructed from immutable observation and candidate digests?
- Do tests prove `applied=false` for every shadow and advisory record?
- Are future production adapters and safety controls clearly separate work?
- Are allowlists, dry-run, rate-limit, and human-approval hooks documented without creating an
  actuation path?
- Can recommendations map to operator-facing knobs while remaining advisory only?

Proceed only when every answer is yes or an exception is documented and approved.

## Planned Demo

```bash
nexuslab shadow --adapter mock \
  --observations examples/shadow/recorded-cluster-observations.jsonl \
  --policy queue_aware --audit results/shadow-audit.jsonl
nexuslab advisory --audit results/shadow-audit.jsonl --decision latest
```

The demo shows the same policy reacting to simulation, replay, and mock observation inputs, then
opens the audit chain and verifies `applied=false`.

## Permitted Evidence Claim

Only after Gate 16 and measured evidence: “Designed a backend-neutral controller boundary and ran
the same versioned policy path in deterministic simulation, trace replay, and mock shadow mode with
auditable non-applied decisions.”

## Honesty Statement

NexusLab will never claim that this phase controls, remediates, validates, or is safe to operate a
real cluster. A mock observation source is not live production telemetry, and advisory output is not
an automated action.

## NexusBench and Adoption Link

The same versioned policy artifact evaluated by NexusBench must run against equivalent backend-
neutral observations without simulator-private dependencies. Completing this cluster with the mock
adapter and non-applied audit unlocks Adoption Stage 2; it does not establish Stage 3 calibration or
Stage 4 partner use.

---

# Cluster 17 — Performance Engineering

## Objective

Demonstrate deliberate systems performance work and provide a measured scale story for NexusBench
matrices without weakening determinism or inventing achieved scale claims.

## Dependencies

- benchmarkable implementations from the owning clusters;
- Cluster 11 matrix execution, Cluster 12 result sizing, and Cluster 24 digest contracts.

## In Scope

- profiling, scale/memory/event-rate measurements, local parallel experiment execution, regression
  guardrails, and evidence-based optimization;
- measured exploration of 2,000- and 10,000-GPU topology what-ifs and overnight policy matrices;
- a future multi-fidelity ADR only when complete measurements identify matrix cost as the limiting
  factor.

## Out of Scope

- declaring aspirational scale classes achieved before architecture-gate evidence exists;
- multi-node distributed simulation before trusted single-process NexusBench results;
- multi-fidelity behavior in NexusBench-v0 or silent changes to scenario/digest semantics.

## Areas

- event object size;
- heap behavior;
- allocation frequency;
- memory pools;
- cache locality;
- graph lookup;
- route caching;
- telemetry overhead;
- serialization throughput;
- replay size;
- parallel experiment execution.

## Deliverables

- performance budget;
- profiler reports;
- flame graphs;
- benchmark history;
- optimization notes;
- regression thresholds.

## Rules

- measure before optimizing;
- preserve before-and-after results;
- do not replace clarity with complexity without evidence;
- keep deterministic behavior;
- document every non-obvious optimization.

## Scale Targets

Targets should be finalized after MVP benchmarks.

Initial aspirational targets:

- 2,048 simulated GPUs on a developer machine;
- measured 2,000- and 10,000-GPU topology what-if classes where feasible;
- overnight policy matrices with an explicit hardware and run-count budget;
- millions of events per second in release mode;
- deterministic replay hashes;
- manageable memory usage;
- parallel experiment execution across CPU cores.

## Acceptance Criteria

- every scale or speed claim links to environment, inputs, commands, and retained measurements;
- regression thresholds follow an established baseline rather than preceding it;
- deterministic digests remain identical before and after performance-only changes;
- 2,000/10,000-GPU and overnight-matrix goals are labeled measured, unmet, or blocked—never implied;
- any multi-fidelity proposal defines accuracy, comparability, and digest consequences in a future
  ADR before implementation.

## Architecture Gate 17

The review must answer yes or no: are bottlenecks measured; are optimizations evidence-based; are
determinism and frozen-suite semantics preserved; are scale claims reproducible; and is any proposed
multi-fidelity level explicit rather than an undocumented shortcut?

## Planned Demo and NexusBench/Adoption Link

Run a bounded scale sweep and a representative NexusBench matrix, publish environment-tagged
throughput/memory/artifact-size results, and verify digest equality across the optimized path. This
supports the C4 adoption scale story but unlocks no adoption stage and is not a v0 blocker.

---

# Cluster 18 — Testing Strategy

## 18.1 Unit Tests

Cover:

- event ordering;
- queue behavior;
- topology generation;
- routing;
- scheduling;
- collective accounting;
- metrics;
- serialization;
- failure semantics.

## 18.2 Integration Tests

Cover:

- small end-to-end job;
- multi-job scenario;
- failure scenario;
- policy comparison;
- replay generation;
- dashboard replay load.

## 18.3 Golden Tests

Store small expected outputs for:

- known topology;
- known event sequence;
- known metrics;
- known policy decisions.

## 18.4 Property Tests

Examples:

- bytes never become negative;
- completed bytes never exceed total bytes;
- routes contain connected links;
- failed links carry no new traffic;
- job completion occurs after arrival;
- simulation time never moves backward;
- resource allocation never exceeds capacity.

## 18.5 Determinism Tests

- identical run hashes;
- identical result manifests;
- stable event order;
- policy reproducibility.

## 18.6 Performance Tests

- event throughput;
- topology generation;
- route selection;
- serialization;
- telemetry overhead.

## 18.7 Fault Tests

- malformed config;
- missing result files;
- corrupted replay;
- disconnected graph;
- impossible job;
- policy exception or failure.

---

# Cluster 19 — Documentation

## Required Documents

- `README.md`
- `NEXUSLAB_MASTER_PLAN.md`
- `ARCHITECTURE.md`
- `ROADMAP.md`
- `CONTRIBUTING.md`
- `docs/concepts/distributed-training.md`
- `docs/concepts/collectives.md`
- `docs/concepts/clos-topology.md`
- `docs/design/simulation-model.md`
- `docs/design/telemetry.md`
- `docs/design/replay-format.md`
- `docs/benchmarks/methodology.md`
- `docs/limitations.md`
- ADRs.

## README Must Include

- project description;
- animated or video demo;
- architecture diagram;
- quick start;
- sample scenario;
- sample comparison;
- core metrics;
- limitations;
- roadmap;
- build instructions;
- contribution instructions.

## Documentation Quality Gate

A reviewer should be able to answer:

- What problem does NexusLab solve?
- What does it simulate?
- What does it simplify?
- How are policies compared?
- How is determinism achieved?
- How would it connect to a real cluster?
- Which results are synthetic?

---

# Cluster 20 — Advanced Parallelism Workloads

## Objective

Extend the completed synthetic workload and collective foundations into first-class data-parallel
(DP), tensor-parallel (TP), pipeline-parallel (PP), and expert-parallel (EP) traffic models. The
goal is behaviorally faithful communication shape, synchronization, and idle attribution that can
support controlled policy comparisons, not framework execution or tensor arithmetic.

## Dependencies

- completed Clusters 5–7 workload, Ring, and placement boundaries;
- Cluster 8 telemetry catalog and summary equivalence;
- Clusters 11–12 experiment provenance and replay schemas;
- Cluster 24 NexusBench scenario, policy-submission, metric, and digest contracts;
- ADR-014 approved before implementation.

## In Scope

- DP AllReduce and reduce-scatter plus all-gather traffic;
- TP groups with frequent smaller AllGather, ReduceScatter, and synchronization collectives;
- PP point-to-point activation and gradient transfers, microbatch schedules, pipeline bubbles, and
  stage idle time;
- EP dispatch/combine AllToAll approximation with configurable skew, fan-out, and incast;
- deterministic composition of DP, TP, PP, and EP groups in one synthetic job;
- metrics for bubble time, collective wait, pipeline wait, communication-caused GPU idle, group
  bytes, and cross-rail traffic once Cluster 21 supplies rail identity.

## Out of Scope

- PyTorch graph execution, tensor values, kernels, memory allocation, or optimizer math;
- bit-exact NCCL algorithm selection or channel scheduling;
- claiming that synthetic phase durations predict a specific GPU generation;
- every framework-specific parallelism strategy or dynamic expert router.

## Public Interfaces and Schemas to Design Before Coding

- versioned `ParallelismPlan`, `ParallelGroup`, rank, stage, microbatch, and expert-group identities;
- planner contracts for AllReduce, AllGather, ReduceScatter, point-to-point, and AllToAll-shaped
  transfers without coupling planners to routing;
- YAML workload schema for DP/TP/PP/EP degrees, microbatches, tensor sizes, expert skew, and overlap;
- lifecycle records for phase ready/start/wait/complete and causal links to collectives/transfers;
- metric definitions and units for bubble, wait, idle, group traffic, and cross-rail bytes;
- deterministic limits on groups, stages, microbatches, experts, collectives, and generated traffic.

## Acceptance Criteria

- tiny DP, TP, PP, and EP cases match analytically derived transfer counts, bytes, dependencies, and
  completion times under ideal links;
- DP supports both Ring AllReduce and explicit reduce-scatter plus all-gather traffic plans;
- PP tests expose bubbles and stage idle time for an imbalanced pipeline;
- EP tests produce deterministic AllToAll/incast traffic with exact byte conservation;
- changing routing or placement does not change the logical parallelism plan or seed digest;
- telemetry attributes collective wait, pipeline wait, bubble time, and communication-caused idle
  without double counting;
- combined DP/TP/PP/EP scenarios are deterministic, bounded, serializable, and replayable.

## Architecture Gate 20

The review must answer yes or no:

- Are parallel groups and dependencies explicit rather than inferred from metric output?
- Are collective/workload planning, routing, and transport still separate?
- Can each model be analytically verified on a small case?
- Are bubble and wait intervals mutually exclusive where the metrics claim partitioning?
- Does EP skew create controlled incast without embedding a routing-policy preference?
- Are limits and unsupported compositions rejected before large allocations?
- Are all fidelity assumptions and synthetic labels visible in scenarios and results?

Proceed only when every answer is yes or an exception is documented and approved.

## Planned Demo

```bash
nexuslab compare examples/parallelism/dp-tp-pp-ep.yaml \
  --routing ecmp,queue_aware --placement first_fit,compact --seed 42020
nexuslab report results/parallelism-comparison --metrics \
  job_completion_time_ns,pipeline_bubble_time_ns,collective_wait_ns,gpu_idle_ns
```

The narrative follows one microbatch through TP collectives, PP transfers, EP dispatch/combine, and
DP synchronization, then explains where each stage waited.

## Permitted Evidence Claim

Only after Gate 20 and measured evidence: “Implemented deterministic DP/TP/PP/EP workload planners
with byte-conserving collective and point-to-point traffic plus pipeline-bubble and wait
attribution.”

## Honesty Statement

NexusLab will never claim that these planners execute a framework, reproduce tensor math, or match
NCCL/kernel timing exactly. They are configurable behavioral traffic models for comparative study.

## NexusBench and Adoption Link

This cluster unlocks the planned NB-Collective track for DP, TP-heavy, PP-bubble, and EP all-to-all
scenarios. It extends NexusBench beyond v0 and does not claim framework or hardware fidelity.

---

# Cluster 21 — NVLink Domains and Multi-Rail Fabric

## Objective

Extend the single-fabric topology and transport model into a heterogeneous hierarchy with
intra-node NVLink- or NVSwitch-like high-bandwidth domains and inter-node multi-rail IB/RoCE-like
connectivity. Placement, routing, telemetry, and failure logic must understand domain and rail
identity while preserving chunk-level deterministic simulation.

## Dependencies

- completed Clusters 2–4 topology, transport, and routing boundaries;
- Cluster 7 placement interface;
- Clusters 8–9 telemetry and failure lifecycle;
- Cluster 20 parallel-group traffic requirements;
- Cluster 24 NexusBench scenario and metric contracts;
- ADR-015 approved before implementation.

## In Scope

- typed link classes and high-bandwidth-domain, node, NIC, rail, leaf, and spine relationships;
- configurable intra-node NVLink/NVSwitch-like bandwidth domains;
- multiple independent inter-node rails with rail-aware path enumeration and policy hooks;
- rail-local and cross-rail placement metrics and constraints;
- rail loss, NIC loss, high-bandwidth-domain degradation, and spine failure interacting with rails;
- chunk-level queueing, serialization, propagation, failure, and recovery on every modeled link class.

## Out of Scope

- Verbs queue pairs, RDMA packet headers, congestion notification protocol fidelity, credit models,
  retransmission engines, or vendor firmware;
- exact NVLink/NVSwitch topology for a proprietary server or exact IB/RoCE deployment behavior;
- real device discovery, switch programming, or link control.

## Public Interfaces and Schemas to Design Before Coding

- stable topology identities for node, high-bandwidth domain, rail, and link class;
- versioned topology schema describing GPU-to-domain and NIC-to-rail attachment;
- `FabricPath`/`FabricView` extensions that expose rail and link class without leaking mutable graph
  ownership;
- rail-aware routing candidate and placement-constraint contracts;
- failure targets and degradation values for rail, NIC, domain, and spine components;
- telemetry labels/records for per-rail utilization, cross-rail bytes, fallback, and degraded-domain
  impact;
- deterministic validation and scale limits for rails, attachments, and candidate paths.

## Acceptance Criteria

- canonical small and 512-GPU heterogeneous topologies validate and serialize byte-identically;
- intra-node traffic remains in its configured high-bandwidth domain unless the scenario explicitly
  models a fallback path;
- rail-aware routing and placement receive typed rail data and deterministic candidate order;
- per-rail and cross-rail byte totals conserve the generated workload bytes;
- rail, NIC, domain-degradation, and spine failures invalidate only affected paths and produce
  explicit recovery outcomes;
- a rail-loss scenario demonstrates deterministic surviving-rail fallback or explicit no-route;
- topology generation, route lookup, memory, and event-volume baselines are recorded before
  regression thresholds are proposed.

## Architecture Gate 21

The review must answer yes or no:

- Are domain, node, NIC, rail, switch, port, and link identities unambiguous?
- Can policies reason about rails through bounded read-only views?
- Are local-domain and inter-node timing/accounting separated without double counting?
- Do failures compose correctly when a spine and one rail are unavailable together?
- Are topology and route explosion bounded at the 512-GPU target and measured at 2,048 GPUs?
- Does the model remain behavioral and chunk-level rather than drifting into RDMA protocol work?
- Are every hardware analogy and calibration limitation documented?

Proceed only when every answer is yes or an exception is documented and approved.

## Planned Demo

```bash
nexuslab compare examples/fabric/multirail-ep-incast.yaml \
  --placement first_fit,rail_aware --routing ecmp,queue_aware \
  --failure rail-1-down --seed 42021
nexuslab replay results/multirail-ep-incast --show rails,cross_rail_bytes,domain_idle
```

The narrative contrasts local high-bandwidth traffic, rail-balanced inter-node traffic, and the
fallback path after a rail loss combined with a spine failure.

## Permitted Evidence Claim

Only after Gate 21 and measured evidence: “Extended a deterministic GPU-cluster simulator with
NVLink-domain and multi-rail fabric abstractions, rail-aware policy hooks, and composable rail/NIC/
spine failures at measured scale.”

## Honesty Statement

NexusLab will never describe this model as a full Verbs/RDMA, NCCL, NVLink, InfiniBand, or RoCE
implementation, nor claim hardware-equivalent latency or throughput without external calibration.

## NexusBench and Adoption Link

This cluster adds rail-aware dimensions to NB-Routing, NB-Placement, NB-Collective, and NB-Recovery.
It is also the topology basis for advisory placement constraints and routing recommendations, but it
does not itself unlock a production or shadow stage.

---

# Cluster 22 — Trace Import and Trace-Driven Replay

## Objective

Import recorded collective and training-step behavior into a versioned NexusLab intermediate schema,
reconstruct deterministic jobs and transfers, and replay the same recorded step pattern under
alternate routing, placement, topology, and failure policies. This enables the defensible comparison
“same recorded step pattern, different infrastructure policy” while keeping source-specific parsing
outside simulation-domain interfaces.

## Dependencies

- Cluster 8 telemetry identifiers and schema versioning;
- Cluster 11 deterministic matrices and provenance;
- Cluster 12 durable result/replay format and integrity checks;
- Clusters 20–21 parallelism and heterogeneous-fabric schemas;
- Cluster 24 NexusBench scenario and comparison contracts;
- ADR-016 approved before implementation.

## In Scope

- a NexusLab-owned intermediate JSON/JSONL schema for ranks, steps, phases, collective/point-to-point
  operations, bytes, relative timestamps, dependencies, and source provenance;
- importer validation, normalization, content digests, and deterministic identity assignment;
- reconstruction of synthetic jobs, collectives, transfers, and causal dependencies;
- replay under alternate routing, placement, and failure configurations with locked imported work,
  seed, simulator version, importer version, and schema digest;
- documented optional mappings from NCCL/nsys-like exported concepts into the intermediate schema;
- explicit warnings for missing, ambiguous, clipped, or unsupported source fields.

## Out of Scope

- parsing every native proprietary trace format in the first implementation;
- recovering tensors, kernel execution, network packets, or undocumented NCCL state;
- bit-exact reproduction of NCCL/RDMA ordering, timing, or algorithm selection;
- using trace-derived results as proof of real-cluster speedup.

## Public Interfaces and Schemas to Design Before Coding

- `nexuslab.training_trace` intermediate schema and supported-version policy;
- `TraceImporter`, source-mapping report, warning/error model, and import limits;
- canonical trace digest and source/importer/simulator provenance fields;
- reconstruction contract mapping ranks, phases, dependencies, and operations into Cluster 20 plans;
- replay override schema defining exactly which infrastructure policies may change while workload
  content remains locked;
- comparison manifest proving imported-work digest equality across alternatives.

## Acceptance Criteria

- a hand-checkable fixture imports to a canonical byte-stable intermediate trace;
- malformed, unsupported, ambiguous, oversized, and dependency-cyclic inputs fail explicitly;
- identical source content and importer version produce identical IDs, intermediate bytes, and
  digests;
- reconstructed operation counts, bytes, rank groups, and dependencies equal the intermediate trace;
- two or more policy replays preserve the imported-work digest while changing only declared
  infrastructure configuration;
- repeated replays with identical seed and versions produce identical domain-outcome digests;
- import mapping reports and results clearly label approximations and trace-derived values.

## Architecture Gate 22

The review must answer yes or no:

- Is the intermediate schema owned by NexusLab and independent of one vendor tool?
- Is source parsing isolated from workload, collective, routing, and transport engines?
- Can a reviewer prove that policy comparisons used the same imported step pattern?
- Are importer, schema, source-content, simulator, seed, and policy digests preserved?
- Are missing or ambiguous source semantics reported rather than guessed silently?
- Are compatibility, size, recursion/dependency, and malformed-input limits tested?
- Do all outputs avoid bit-exact or hardware-speedup claims?

Proceed only when every answer is yes or an exception is documented and approved.

## Planned Demo

```bash
nexuslab trace import examples/traces/recorded-step-v1.jsonl \
  --out results/imported-step
nexuslab trace replay results/imported-step \
  --routing ecmp,queue_aware --placement first_fit,rail_aware \
  --failure examples/failures/spine-link.yaml --seed 42022
nexuslab compare results/imported-step/replays --require-workload-digest-match
```

The demo displays one imported operation DAG and verifies that every comparison shares its digest
before showing policy-dependent queueing, idle, and completion outcomes.

## Permitted Evidence Claim

Only after Gate 22 and measured evidence: “Built a versioned training-trace import pipeline that
reconstructed deterministic jobs/transfers and replayed an identical recorded step pattern under
alternate routing, placement, and failure policies.”

## Honesty Statement

NexusLab will never claim bit-exact NCCL, nsys, GPU-kernel, or RDMA replay. Imported traces are
incomplete observations normalized into an approximate behavioral workload model.

## NexusBench and Adoption Link

This cluster unlocks NB-Trace and Adoption Stage 1. Its canonical intermediate schema is also the
ingest boundary for planned NCCL-like logs, nsys-like timelines, and DCGM/Prometheus-style counters;
source adapters must document mappings, omissions, anonymization, and provenance.

---

# Cluster 23 — Flagship Reproducible A/B Study

## Objective

Publish one required NexusBench reference study that answers a focused infrastructure question with saved run
receipts. The initial study asks whether queue-aware routing improves tail completion and idle time
versus ECMP for the same trace-derived EP incast pattern during a controlled spine-link failure on
the approved heterogeneous multi-rail topology; the measured result may be positive, neutral, or
negative.

## Dependencies

- gate-approved Clusters 8–9, 11–12, 20–22, and 24;
- Cluster 14 analysis/report generation;
- measured metric definitions and real completed runs; no study document is populated beforehand.

## In Scope

- locked topology, imported workload digest, seed set, simulator/importer versions, metric catalog,
  failure schedule, and policy versions;
- ECMP and queue-aware routing A/B comparison, with placement fixed unless an explicitly separate
  placement factor is approved;
- at least three repeats per configuration, or a documented evidence-based reason for another count;
- run IDs, manifests, input/output digests, commands, environment notes, plots, and raw-result links;
- confidence and model caveats, negative/neutral findings, and synthetic/trace-derived labels;
- `docs/studies/README.md` for study methodology and
  `docs/studies/ep-incast-spine-ab.md` for the completed evidence artifact.

## Out of Scope

- invented numbers, placeholder charts, hand-edited results, cherry-picked seeds, or omitted failures;
- causal claims beyond the locked model and experiment matrix;
- claims of production GPU-cluster improvement or statistical confidence unsupported by repeats;
- changing simulator behavior merely to make the result more impressive.

## Public Interfaces and Schemas to Design Before Running

- versioned study manifest referencing immutable scenario/trace, topology, failure, and policy inputs;
- repeat/seed policy and comparison-cell identifiers;
- report schema linking every table/plot to metric definition, unit, aggregation, and source run IDs;
- confidence/caveat and synthetic/trace-derived labeling fields;
- command that verifies manifest, workload-digest equality, run completeness, and result digests before
  report generation.

## Acceptance Criteria

- the entire matrix runs from one documented command and every required cell completes;
- at least three repeats exist per cell unless the report records and justifies a different design;
- all A/B cells share the locked imported-work, topology, failure, and seed-set digests;
- the Markdown study contains no placeholders and every number/plot is regenerated from saved results;
- summary claims use defined units and aggregations and preserve neutral or adverse findings;
- a clean-clone reviewer can verify digests and regenerate the report without manual spreadsheet work;
- the report clearly states that results are simulated from an approximate trace-derived workload.

## Architecture Gate 23 — Reference-Study Evidence Gate

The review must answer yes or no:

- Is there one clear engineering question and a predeclared comparison matrix?
- Are workload, topology, failure, seed, simulator, importer, metric, and policy versions locked?
- Are repeat count and confidence language justified?
- Can every displayed value be traced to immutable run IDs and digests?
- Were negative, neutral, failed, and incomplete runs handled transparently?
- Can the report be regenerated without manual number entry?
- Are synthetic and trace-derived limitations prominent enough to prevent a hardware claim?

Scientific-release-complete status is forbidden until every answer is yes or an exception is documented and
approved.

## Planned Demo

```bash
nexuslab matrix experiments/studies/ep-incast-spine-ab.yaml --repeats 3
nexuslab study verify results/studies/ep-incast-spine-ab
nexuslab study render results/studies/ep-incast-spine-ab \
  --out docs/studies/ep-incast-spine-ab.md
```

The final demo starts from the study manifest, verifies all digests, regenerates the report, and then
walks one representative replay to explain the aggregate result.

## Permitted Evidence Claim

Only after Gate 23, real runs, and published receipts: “Published a reproducible multi-repeat A/B
study comparing ECMP and queue-aware routing for trace-derived EP incast under multi-rail spine
failure, with locked inputs, digests, and explicit model caveats.” Measured values may be added only
after verification.

## Honesty Statement

NexusLab will never present this study as a production benchmark, conceal an unfavorable outcome,
or substitute simulated/trace-derived evidence for measurements from real GPU hardware.

## NexusBench and Adoption Link

This study is the first NexusBench case study/reference result for the trace-driven collective/
routing intersection. It proves the artifact method, not real-cluster validity, and does not advance
the adoption ladder beyond the evidence stages separately achieved by Clusters 22 and 16.

---

# Cluster 24 — NexusBench Specification and Harness

**Execution priority:** Cluster 24 is numbered after the previously reserved research-capability clusters but
is implemented before Cluster 20. Cluster numbers are stable ownership identifiers, not execution
order.

## Objective

Turn NexusBench-v0 into a frozen, versioned, reproducible evaluation contract for policy authors.
The first suite compares routing and placement baselines using existing synthetic capabilities; it
must prove scenario immutability, policy interchange, repeat orchestration, metric definitions, and
digest comparability before advanced tracks are added.

## Dependencies

- gate-approved Clusters 8–9 for metrics and deterministic failure observations;
- gate-approved Clusters 11–12 for matrices, manifests, saved results, replay, and digests;
- Cluster 15 static C++ policy submission contract;
- the minimal Cluster 14 repository-native report path;
- ADR-012 and ADR-013 approved before implementation.

## In Scope

- NexusBench-v0 suite manifest, scenario-pack layout, compatibility/version policy, and frozen IDs;
- NB-Routing-v0 with incast and spine-link failure, using ECMP and queue-aware baselines;
- NB-Placement-v0 with rack-pressure and multi-tenant interference, using first-fit and compact
  placement baselines;
- three-repeat matrices, declared seed sets, primary/secondary metrics, missing/failed-cell rules,
  caveats, and synthetic labels;
- canonical digests over suite/scenario, seed, topology, workload, failure script, simulator, metric
  catalog, policy artifact/configuration, and outcome;
- repository-native Markdown/JSON results suitable for CI artifacts and later leaderboard import;
- citation format, clean-clone reproduction instructions, and “how to add a policy” documentation;
- policy CI behavior that compares deterministic digests and rejects undeclared scenario changes.

## Out of Scope

- a hosted leaderboard, public submission service, or untrusted runtime plugin execution;
- DP/TP/PP/EP, heterogeneous multi-rail, trace-driven, or calibration tracks in v0;
- fabricated reference numbers, placeholder rankings, or claims of statistical significance;
- hardware prediction, packet-accurate RDMA, production control, or design-partner validation;
- changing frozen scenarios in place after v0 is published.

## Public Interfaces and Schemas to Design Before Coding

- `nexusbench.suite` manifest with suite version, scenario IDs, track membership, compatibility
  constraints, repeat/seed policy, and required baseline policies;
- frozen scenario-pack paths and canonical serialization/digest rules;
- policy submission metadata and conformance result linked to the Cluster 15 SDK;
- scoring contract naming primary/secondary metrics, units, aggregations, confidence/caveat fields,
  missing/failed-run treatment, and comparison direction without collapsing metrics into an
  unjustified single score;
- result/leaderboard record linking policy, suite, scenario, run IDs, digests, labels, and artifact
  locations;
- citation block and reproducibility receipt schema;
- suite evolution rules: corrections require an explicit patch policy; behavior-changing scenario,
  metric, seed, or digest changes require a new suite version.

## Acceptance Criteria

- the complete v0 scenario pack is checked into the repository, versioned, documented, and frozen;
- each included track has at least two conforming baseline policies and every required cell runs
  exactly three declared repeats;
- identical suite/scenario, seed, policy artifact/configuration, simulator version, and metric
  catalog reproduce byte-identical canonical input and outcome digests;
- changing any locked dimension changes the appropriate digest or fails validation;
- a policy can be swapped through Cluster 15 without core or scenario-pack edits;
- every result contains metric definitions, provenance, failed/missing-cell state, caveats, and an
  explicit synthetic label;
- Markdown/JSON result summaries regenerate from saved results with no manual number entry;
- clean-clone instructions reproduce the harness and verify artifacts;
- repository search finds no fabricated v0 values, placeholder rankings, or unsupported fidelity
  claims.

## Architecture Gate 24 — NexusBench-v0 Gate

The review must answer yes or no:

- Is every scenario, seed, failure, metric, version constraint, and baseline policy frozen and named?
- Can an independent reviewer distinguish inputs, policy artifact/configuration, and outcomes by
  digest?
- Does every included track have at least two baselines and a complete three-repeat matrix?
- Can a new conforming policy run without simulator-core or scenario-pack changes?
- Are failed and missing cells represented without favorable filtering?
- Are score direction, aggregation, uncertainty/caveat fields, and non-combinable metrics explicit?
- Does suite evolution prevent silent benchmark drift and scenario overfitting?
- Are all results labeled synthetic and all predictive/hyperscaler claims absent?

Proceed only when every answer is yes or an exception is documented and approved. Research-ready
and scientific-release-complete status remain forbidden until this gate passes and the relevant evidence exists.

## Planned Demo

```bash
nexuslab nexusbench validate nexusbench/v0/suite.yaml
nexuslab nexusbench run v0 --track routing --policies ecmp,queue_aware --repeats 3
nexuslab nexusbench run v0 --track placement --policies first_fit,compact --repeats 3
nexuslab nexusbench verify results/nexusbench-v0
```

The demo first runs the two baseline tracks, then swaps one conforming policy and shows that locked
scenario digests remain equal while policy and outcome digests are comparable and distinct where
appropriate.

## NexusBench and Adoption Link

This cluster establishes NexusBench itself and unlocks Adoption Stage 0. Deterministic policy CI
digests support later adoption, but v0 alone does not establish trace ingest, shadow operation,
calibration, design-partner trust, or advisory use.

## Permitted Claim

Only after Gate 24 and real complete harness runs: “Published NexusBench-v0, an open reproducible
benchmark for comparing routing and placement policies under locked synthetic GPU-cluster
scenarios.” Any measured values must come from verified artifacts.

## Honesty Statement

NexusBench-v0 is synthetic comparative evidence. It does not predict real hardware, reproduce
NCCL/RDMA, validate a production controller, or demonstrate hyperscaler adoption.

---

# 20. Architecture Decision Records

Store ADRs in:

```text
docs/adr/
```

## ADR Template

```md
# ADR-XXX: Decision Title

## Status

Proposed / Accepted / Superseded / Rejected

## Context

What problem requires a decision?

## Options Considered

### Option A

Benefits:
- ...

Costs:
- ...

### Option B

Benefits:
- ...

Costs:
- ...

## Decision

What was selected?

## Rationale

Why was it selected?

## Consequences

Positive:
- ...

Negative:
- ...

## Validation

How will this decision be tested?

## Revisit Trigger

What evidence would justify revisiting it?
```

## ADR Registry

Accepted:

- ADR-001: Use discrete-event simulation.
- ADR-002: Use integer nanoseconds for simulated time.
- ADR-003: Use chunk-level rather than packet-level transfers.
- ADR-004: Define deterministic event semantics.
- ADR-005: Define the topology and cluster model, beginning with Clos.
- ADR-006: Define link, queue, and transfer semantics.
- ADR-007: Define deterministic routing at transfer admission.
- ADR-008: Define the synthetic training-workload lifecycle.
- ADR-009: Define Ring AllReduce planning and execution.
- ADR-010: Define bounded admission scheduling and GPU placement.
- ADR-011: Define the deterministic telemetry and observability boundary.

Reserved; each document remains proposed and must be written and accepted before implementation of
its owning cluster:

- ADR-012: NexusBench scenario-pack format, suite versioning, evolution, and canonical digest
  contract (Cluster 24).
- ADR-013: NexusBench policy submission boundary, static C++ SDK constraints, conformance, and
  policy-artifact identity (Clusters 15 and 24).
- ADR-014: DP/TP/PP/EP parallel-group, dependency, and traffic-planner semantics (Cluster 20).
- ADR-015: Heterogeneous NVLink-domain and multi-rail topology identity, routing, placement, and
  failure semantics (Cluster 21).
- ADR-016: Canonical NexusLab intermediate telemetry/training-trace schema, normalization,
  provenance, anonymization metadata, and replay override boundary (Cluster 22).
- ADR-017: Backend-neutral observations, mock shadow capabilities, advisory mappings, safety hooks,
  and non-applied decision audit schema (Cluster 16).
- ADR-018: Calibration/validity definitions for held-out policy-ranking correlation, disagreement
  reporting, and private-trace disclosure boundaries (Cluster 14; later Stage 3 work).

Other planned topics receive an ADR number when work on the owning cluster begins:

- separate policies from simulation core;
- use YAML scenarios;
- separate collective planning from routing;
- store replay independently from the simulator;
- use C++ for the simulation core;
- use React or Next.js for replay visualization;
- record policy decision explanations;
- optimize job completion time rather than only flow latency.

---

# 21. Architecture Review Gate Template

Every cluster ends with this review.

```md
# Architecture Gate — Cluster X

## Correctness

- Are invariants documented?
- Are edge cases tested?
- Can expected behavior be calculated independently?

## Abstraction

- Is the chosen abstraction the correct level?
- Is the interface exposing too much internal state?
- Will future features require rewriting this subsystem?

## Performance

- Has the subsystem been benchmarked?
- Is the memory model acceptable?
- Is telemetry overhead known?

## Extensibility

- Can another implementation be added?
- Are policy boundaries clean?
- Are schemas versioned?

## Failure Behavior

- What happens when input is invalid?
- What happens when the subsystem cannot complete?
- Are failures observable?

## Documentation

- Is the decision recorded?
- Are limitations documented?
- Can another engineer understand the design?

## Decision

Proceed: YES / NO

Required changes:
- ...
```

---

# 22. AI Coding Agent Workflow

## 22.1 Role Split

You act as:

- product owner;
- system architect;
- technical lead;
- reviewer;
- benchmark owner;
- final decision maker.

Claude Code, Codex, or similar agents act as:

- implementation engineers;
- test writers;
- refactoring assistants;
- documentation assistants;
- benchmark automation assistants.

## 22.2 Agent Rules

An agent must not:

- implement multiple clusters in one uncontrolled pass;
- invent architecture without recording it;
- change public interfaces silently;
- add dependencies without approval;
- generate fake benchmark results;
- disable failing tests;
- weaken compiler warnings;
- remove determinism checks;
- claim realism that is not supported.

## 22.3 Required Prompt Structure

Every implementation prompt should include:

1. context;
2. cluster objective;
3. files allowed to change;
4. interfaces;
5. invariants;
6. edge cases;
7. tests required;
8. benchmark required;
9. documentation required;
10. stop condition.

## 22.4 Prompt Template

```md
You are implementing Cluster X of NexusLab.

Read:
- NEXUSLAB_MASTER_PLAN.md
- ARCHITECTURE.md
- relevant ADRs
- existing interfaces

Goal:
[one precise objective]

Do not:
- change public interfaces unless explicitly requested;
- implement future clusters;
- add dependencies without approval;
- weaken tests or warnings.

Required deliverables:
- implementation;
- unit tests;
- integration test;
- benchmark;
- documentation update.

Invariants:
- ...
- ...

Acceptance criteria:
- ...
- ...

Before coding:
1. summarize the current architecture;
2. list assumptions;
3. identify unresolved decisions;
4. stop and ask questions if a decision affects public interfaces.

After coding:
1. list changed files;
2. explain design choices;
3. report tests and benchmarks;
4. identify risks;
5. stop for architecture review.
```

## 22.5 Review Checklist for AI-Generated Code

- Do I understand every public interface?
- Can I explain ownership and lifetimes?
- Are error cases explicit?
- Are tests meaningful?
- Are benchmarks real?
- Did the agent add hidden complexity?
- Is generated code duplicated?
- Are comments accurate?
- Are names domain-correct?
- Did the agent assume behavior not in the plan?
- Can the design be justified in an independent technical review?

---

# 23. Milestones

## Milestone 0 — Foundation

Includes:

- Cluster 0;
- repository;
- CI;
- docs;
- sample executable.

Demo:

```bash
nexuslab --version
```

## Milestone 1 — Simulation Kernel

Includes:

- Cluster 1;
- deterministic events;
- benchmark.

Demo:

```bash
nexuslab simulate examples/clock.yaml
```

## Milestone 2 — Fabric MVP

**Status: Complete — 2026-09-05.** Clusters 2 and 3 are gate-approved. Run
`bash scripts/benchmark-transport.sh --pattern incast --flows 100` to demonstrate transfers
across a generated Clos and report queue buildup and transfer outcomes.

Includes:

- Clusters 2 and 3;
- topology;
- links;
- queues;
- transfer model.

Demo:

- transfer data across a generated Clos topology;
- show queue buildup.

## Milestone 3 — Routing Comparison

**Status: Complete — 2026-09-05.** Run `bash scripts/benchmark-routing-suite.sh` for
the repeatable four-policy comparison and path-lookup scale matrix.

Includes:

- Cluster 4;
- ECMP;
- least-loaded;
- queue-aware.

Demo:

- same traffic;
- three policies;
- summary comparison.

## Milestone 4 — Training Workload MVP

**Status: Complete — 2026-09-05.** Run `nexuslab train --file
examples/training/two-worker.yaml --timeline` after building. Both Clusters 5 and 6 are included;
[scenario documentation](docs/training-scenarios.md) defines the synthetic assumptions and limits.

Includes:

- Clusters 5 and 6;
- jobs;
- Ring AllReduce;
- completion time.

Demo:

- run a multi-GPU training job;
- show compute and communication phases.

## Milestone 5 — Multi-Tenant Cluster

**Status: Complete — 2026-09-05.** Run `train --file examples/training/scheduled.yaml --timeline`
or `bash scripts/benchmark-scheduling-suite.sh` to compare all four placement policies.

Includes:

- Cluster 7;
- scheduling;
- multiple jobs.

Demo:

- compare first-fit and topology-aware placement.

## Milestone 6 — Failures and Telemetry

Includes:

- Clusters 8 and 9.

Demo:

- fail a spine;
- replay congestion and recovery.

## Milestone 7 — Experiment Platform

Includes:

- Clusters 11 and 12.

Demo:

- run a benchmark matrix;
- persist results;
- replay any run.

## Milestone 8 — NexusBench-v0 Specification and Harness

Includes:

- Cluster 24 with the minimum Cluster 14 report path and Cluster 15 policy SDK boundary;
- frozen NB-Routing-v0 and NB-Placement-v0 scenario packs;
- metric/scoring contract, three-repeat matrices, citation format, policy guide, and deterministic
  input/outcome digests.

Demo:

- run both baseline tracks, swap one conforming policy, and verify comparable result packages.

## Milestone 9 — Advanced Parallelism

Includes:

- Cluster 20;
- DP, TP, PP, and EP traffic models;
- bubble, collective-wait, pipeline-wait, and communication-idle attribution.

Demo:

- compare routing and placement for the same composed parallelism plan.

## Milestone 10 — Heterogeneous Multi-Rail Fabric

Includes:

- Cluster 21;
- NVLink/NVSwitch-like high-bandwidth domains;
- rail-aware inter-node topology, policy hooks, telemetry, and failures.

Demo:

- replay EP incast across multiple rails before and after rail/spine failures.

## Milestone 11 — Trace-Driven Replay

Includes:

- Cluster 22;
- versioned intermediate training-trace import;
- same recorded step pattern under alternate infrastructure policies;
- Adoption Stage 1 evidence.

Demo:

- import one recorded-step fixture, verify its digest, and replay policy alternatives.

## Milestone 12 — Flagship NexusBench Evidence

Includes:

- Cluster 23 and the required Cluster 14 reporting/attribution path;
- the first NexusBench case study/reference result;
- a locked, repeated, digest-verifiable A/B matrix;
- `docs/studies/ep-incast-spine-ab.md` generated only from real completed runs.

Demo:

- verify receipts, regenerate the report, inspect one explanatory replay, and show counterfactual
  attribution deltas.

## Milestone 13 — Production-Shaped Shadow Boundary

Includes:

- expanded Cluster 16;
- simulation, replay, mock shadow, and advisory adapters;
- shared policy path and `applied=false` decision audit log;
- Adoption Stage 2 evidence.

Demo:

- feed mock observations through the same policy used in NexusBench and inspect the audit chain.

## Milestone 14 — NexusBench Analysis Dashboard

Includes:

- Cluster 13 after Milestones 8–13;
- replay-only views for NexusBench comparisons, parallelism, rails, imported traces, study receipts,
  attribution, and shadow decisions.

Demo:

- polished replay, comparison, and decision inspector consuming approved schemas.

## Milestone 15 — Advanced Controllers

Includes:

- Cluster 10;
- optional congestion-control experiments and additional policies after the required scientific
  evidence exists.

## Scientific Delivery Priority

Future execution order is: finish Clusters 8–9; implement the necessary Clusters 11–12 experiment
and digest foundation; define the minimum Cluster 15 policy SDK and Cluster 14 report boundary;
implement and gate Cluster 24 NexusBench-v0; then Clusters 20, 21, 22, 23, 16, and the deeper
Cluster 14 attribution path; then Cluster 13. Calibration/validity is later Stage 3 work. Only one
implementation cluster is active at a time. Cluster 10, multi-fidelity work, and optional controller
breadth do not outrank NexusBench-v0 or the required evidence path.

---

# 24. MVP Definition

The MVP is complete when:

- a scenario defines a Clos cluster;
- two or more training jobs execute;
- jobs use Ring AllReduce;
- network transfers create queueing;
- ECMP, least-loaded, and queue-aware routing can be selected;
- one failure can be injected;
- metrics include job completion time, GPU idle time, queue depth, link utilization, and drops;
- identical seeds produce identical results;
- a benchmark comparison can be run from CLI;
- results can be persisted, validated, and inspected through the replay CLI/schema;
- the README contains a complete reproducible demo.

Anything beyond this is not required for the thin first public release. Completing this MVP is an
important runnable checkpoint, but it does not mean the scientific release or NexusBench research
program is complete; Section 25 defines the additional evidence requirements.

---

# 25. Scientific Release, Research, and Adoption Readiness

## 25.1 Scientific Release Definition

The planned scientific release is complete only when the thin MVP, NexusBench-v0, one measured
reference study, and all five gated supporting capabilities are complete:

- Cluster 24 publishes NexusBench-v0 with its frozen routing and placement tracks, deterministic
  digests, baseline policy matrices, metric dictionary, policy guide, and reproducible artifacts;
- Cluster 20 demonstrates first-class DP/TP/PP/EP traffic and measured bubble/wait/idle attribution;
- Cluster 21 demonstrates NVLink-domain and multi-rail behavior with rail-aware placement/routing
  hooks and composable rail, NIC, domain, and spine failures;
- Cluster 22 imports a versioned trace, reconstructs work deterministically, and proves identical
  workload digests across alternate-policy replays;
- Cluster 23 publishes the receipt-backed flagship A/B study from real completed runs;
- Cluster 16 runs one policy path across simulation, replay, and mock shadow inputs with an auditable
  `applied=false` record;
- Architecture Gates 24, 20, 21, 22, 23, and 16 are approved and ADR-012 through ADR-017 are
  accepted;
- every permitted public or publication claim is backed by measured evidence and uses the required
  honesty text.

The scientific release package also includes:

- clean public repository;
- complete setup and clean-clone reproduction instructions;
- versioned benchmark specification and metric dictionary;
- immutable scenario packs, run manifests, digests, and result receipts;
- generated benchmark and reference-study reports;
- documented methods, architecture, fidelity assumptions, limitations, and negative results;
- no fabricated or manually transcribed measurements;
- a tagged scientific release and citable version identifier;
- a technical report or paper-style write-up suitable for independent review.

The dashboard improves explanation, but it cannot compensate for a missing capability, gate,
digest, run receipt, or limitation statement.

## 25.2 Research-Ready Definition

NexusBench is research-ready when Gate 24 passes; the versioned specification, frozen v0 pack,
baseline policies, metric and scoring contract, citation format, three-repeat harness, and clean-
clone artifact instructions are published; and actual complete v0 artifacts can be regenerated.
Research-ready does not require calibration, production traces, a hosted leaderboard, or a polished
dashboard, and it does not permit real-cluster prediction claims.

## 25.3 Adoption-Ready Definition

“Adoption-ready” does not mean production control-plane adoption. It means a real infrastructure
team could evaluate versioned policies in reproducible shadow/advisory workflows. The minimum gate
requires Adoption Stage 2 plus a documented canonical ingest path, deterministic policy-CI digests,
honesty/safety documentation, and the checklist below. Stage 3 design-partner calibration is the
major trust unlock and remains visibly open until real or semi-real held-out evidence exists.

## 25.4 Hyperscaler Adoption Requirements

NexusLab v1 does not claim hyperscaler production control-plane adoption. These requirements are
non-negotiable before public adoption-ready language:

- [ ] **C1 — Held-out ranking correlation:** publish a protocol comparing policy rankings, not just
  absolute times, between simulation and real or semi-real measurements. Report Spearman, Kendall,
  or an approved equivalent plus uncertainty and disagreement/failure cases. Private partner traces
  are acceptable only when the public method, canonical schema, and anonymization rules are clear.
- [ ] **C2 — Existing-data ingest:** define adapters/mappings for NCCL-like collective logs,
  nsys-like timelines, and DCGM/Prometheus-style counters through one versioned NexusLab canonical
  intermediate schema. Every adapter reports mappings, omissions, provenance, and unsupported data.
- [ ] **C3 — Shadow-only default:** decide but never apply. Audit every observation → candidates →
  decision → `applied=false` chain; document allowlists, dry-run behavior, rate limits, and human-
  approval hooks; ban auto-remediation and switch programming in this phase.
- [ ] **C4 — Measured scale story:** treat 2,000- and 10,000-GPU topology what-ifs and overnight
  policy matrices as measurement goals, not achieved results. Record architecture-gate measurements
  before claims or regression thresholds; retain the CI guardrail philosophy established at
  Cluster 1.
- [ ] **C5 — Deterministic policy CI:** identical suite/scenario, seed, policy artifact/configuration,
  simulator version, and metric catalog produce identical digests, allowing policy changes to be
  gated like infrastructure-behavior unit tests.
- [ ] **C6 — One design-partner path:** pursue at least one external trace source or partner cluster
  island. Synthetic NexusBench-v0 continues without it, but adoption-ready remains open. Document
  anonymization, ownership, license, retention, and nondisclosure constraints for imported traces.
- [ ] **C7 — Real-knob advisory output:** map recommendations where possible to placement
  constraints/affinities, routing policy choices or weights, collective/NCCL configuration hints,
  and abstract scheduler-preference objects. Do not build a full Kubernetes operator or apply path.
- [ ] **C8 — Honesty and safety:** publish limitations, fidelity assumptions, synthetic/trace/mock
  labels, the safety model, and non-goals before any adoption language.

## 25.5 Adoption Maturity Ladder

| Stage | Evidence and capability | Claim boundary |
|---|---|---|
| 0 — Synthetic NexusBench-v0 | Frozen routing/placement packs, baselines, three-repeat harness, metrics, digests | Open synthetic comparative benchmark only |
| 1 — Trace counterfactual | Public, synthetic-derived, or properly governed trace mapped to canonical schema and replayed under locked alternate policies | Approximate trace-derived comparison only |
| 2 — Mock shadow audit | Same policy artifact consumes backend-neutral mock observations and emits complete `applied=false` audits | Production-shaped, non-applied shadow boundary only |
| 3 — Design-partner calibration | Held-out real/semi-real ranking-correlation study with failures and governance documented | Calibration evidence for the measured domain, not universal prediction |
| 4 — Partner advisory workflow | Human-reviewed recommendations map to operator-facing knobs in a partner workflow | Advisory use only unless a separate future safety program approves actuation |

Stages never bypass C8 honesty and safety requirements. A later stage does not erase the fidelity,
data-governance, or non-actuation limits of earlier evidence.

---

# 26. Demo Scenarios

## Scenario A — ECMP Hotspot

Purpose:

- show static ECMP causing uneven utilization;
- compare least-loaded and queue-aware policies.

## Scenario B — Multi-Tenant Interference

Purpose:

- show one large job delaying another;
- compare placement policies.

## Scenario C — Spine Failure

Purpose:

- show detection, reroute, and recovery;
- compare completion-time impact.

## Scenario D — Ring vs Tree

Purpose:

- compare collective strategies by message size and topology.

## Scenario E — Checkpoint Burst

Purpose:

- inject background traffic;
- observe GPU idle time and queue growth.

## Scenario F — Stale Telemetry

Purpose:

- test whether an adaptive policy becomes unstable.

## Scenario G — Composed DP/TP/PP/EP Training

Purpose:

- show frequent TP collectives, PP activation/gradient traffic and bubbles, skewed EP AllToAll, and
  DP synchronization in one deterministic job;
- compare JCT, pipeline bubble, collective wait, and communication-caused GPU idle.

Planned command:

```bash
nexuslab compare examples/parallelism/dp-tp-pp-ep.yaml \
  --routing ecmp,queue_aware --placement first_fit,compact --seed 42020
```

## Scenario H — Multi-Rail Degradation

Purpose:

- compare first-fit and rail-aware placement on an NVLink-domain plus multi-rail topology;
- fail one rail and a spine link, then inspect cross-rail traffic and fallback/no-route behavior.

Planned command:

```bash
nexuslab compare examples/fabric/multirail-ep-incast.yaml \
  --placement first_fit,rail_aware --failure rail-1-down,spine-link-down --seed 42021
```

## Scenario I — Same Recorded Step, Different Policy

Purpose:

- import one NexusLab intermediate trace and lock its content digest;
- replay the same reconstructed jobs/transfers under ECMP and queue-aware routing with alternate
  placement or failure policy declared separately.

Planned commands:

```bash
nexuslab trace import examples/traces/recorded-step-v1.jsonl --out results/imported-step
nexuslab trace replay results/imported-step --routing ecmp,queue_aware \
  --require-workload-digest-match --seed 42022
```

## Scenario J — Flagship EP Incast A/B Study

Purpose:

- answer whether queue-aware routing changes tail completion and idle time versus ECMP for the same
  trace-derived EP incast pattern during a spine-link failure;
- verify the locked matrix and regenerate the published report from at least three repeats per cell.

Planned commands:

```bash
nexuslab matrix experiments/studies/ep-incast-spine-ab.yaml --repeats 3
nexuslab study verify results/studies/ep-incast-spine-ab
nexuslab study render results/studies/ep-incast-spine-ab \
  --out docs/studies/ep-incast-spine-ab.md
```

## Scenario K — Mock Shadow Decision Audit

Purpose:

- run the same policy code against simulation, replay, and ordered mock cluster observations;
- inspect observed state, candidates, chosen action, and `applied=false` for every decision.

Planned command:

```bash
nexuslab shadow --adapter mock \
  --observations examples/shadow/recorded-cluster-observations.jsonl \
  --policy queue_aware --audit results/shadow-audit.jsonl
```

## Scenario L — NexusBench-v0 Baseline Tracks

Purpose:

- run the frozen routing and placement scenario packs with two baselines per track;
- verify the declared three-repeat matrices, synthetic labels, metric dictionary, and deterministic
  input/outcome digests.

Planned commands:

```bash
nexuslab nexusbench run v0 --track routing --policies ecmp,queue_aware --repeats 3
nexuslab nexusbench run v0 --track placement --policies first_fit,compact --repeats 3
nexuslab nexusbench verify results/nexusbench-v0
```

## Scenario M — NexusBench Policy Swap

Purpose:

- validate one policy against the static C++ submission contract;
- replace one baseline without editing frozen scenarios, then compare scenario, policy, and outcome
  digests in the repository-native result format.

Planned commands:

```bash
nexuslab policy validate examples/policies/sample_queue_policy
nexuslab nexusbench run v0 --track routing --policy sample_queue_policy --repeats 3
nexuslab nexusbench compare results/nexusbench-v0 --require-scenario-digest-match
```

---

# 27. Benchmark Suite

## Suite 1 — Correctness

- tiny topologies;
- analytically verifiable timings;
- deterministic outputs.

## Suite 2 — Routing

- ECMP;
- least-loaded;
- queue-aware;
- different load patterns.

## Suite 3 — Scheduling

- first-fit;
- compact placement;
- topology-aware placement.

## Suite 4 — Failures

- link failure;
- spine failure;
- GPU failure;
- degraded link.

## Suite 5 — Scale

- 64 GPUs;
- 512 GPUs;
- 2,048 GPUs;
- 8,192 GPUs if feasible.

## Suite 6 — Telemetry Overhead

- off;
- summary;
- sampled;
- full trace.

## Suite 7 — Sensitivity

- chunk size;
- queue size;
- telemetry interval;
- workload mix;
- failure detection delay.

## Suite 8 — Parallelism Traffic Models

- analytical DP AllReduce and reduce-scatter/all-gather cases;
- TP collective frequency and size sweeps;
- PP microbatch/bubble and stage-imbalance sweeps;
- EP skew, fan-out, AllToAll, and incast sweeps;
- combined DP/TP/PP/EP determinism and byte conservation.

## Suite 9 — Heterogeneous and Multi-Rail Fabric

- intra-domain versus inter-node traffic;
- one, two, and four rails;
- first-fit versus rail-aware placement;
- rail, NIC, domain-degradation, and rail-plus-spine failure;
- 512-GPU baseline and 2,048-GPU stretch measurements.

## Suite 10 — Trace Import and Replay

- canonical import and malformed/schema-drift fixtures;
- source/importer/intermediate digest stability;
- reconstruction byte and dependency equivalence;
- same-workload-digest alternate-policy replay;
- import and replay throughput, memory, and file size.

## Suite 11 — Flagship Study Receipts

- locked A/B manifest and predeclared repeats;
- run completeness and digest verification;
- report regeneration from saved results;
- confidence/caveat and synthetic/trace-derived label validation.

## Suite 12 — Shadow Boundary

- same-policy equivalence across simulation, replay, and mock observations;
- stale, late, malformed, and overflowing observation streams;
- capability rejection and fail-closed apply attempts;
- deterministic audit log and universal `applied=false` validation.

## Suite 13 — NexusBench-v0 Contract

- frozen NB-Routing-v0 and NB-Placement-v0 scenario-pack validation;
- ECMP versus queue-aware and first-fit versus compact baseline matrices;
- exactly three declared repeats per required v0 cell;
- suite/scenario, seed, policy artifact/configuration, simulator, metric-catalog, and outcome digest
  stability;
- policy-submission conformance and frozen-scenario isolation;
- Markdown/JSON result regeneration, citation receipts, failed-cell handling, and synthetic-label
  validation.

---

# 28. Risks and Mitigations

## Risk: Scope Explosion

Mitigation:

- MVP definition;
- cluster gates;
- non-goal list;
- release milestones.
- protect NexusBench-v0 from simultaneous ns-3-like fidelity, hosted-leaderboard, dashboard, and
  production-controller ambitions;
- admit advanced tracks only after their supporting gates pass.

## Risk: Fake Realism

Mitigation:

- document assumptions;
- use comparative claims;
- analytically validate simple scenarios;
- clearly label synthetic workloads.

## Risk: Too Many Events

Mitigation:

- chunk-level model;
- configurable fidelity;
- telemetry sampling;
- benchmark event counts;
- memory pools only after profiling.

## Risk: Beautiful UI Before Correct Core

Mitigation:

- replay dashboard starts after telemetry schema is stable;
- CLI summaries first;
- frontend never defines simulation behavior.

## Risk: AI-Generated Architecture Drift

Mitigation:

- one cluster per prompt;
- file-change restrictions;
- ADRs;
- review gates;
- no silent interface changes.

## Risk: Unexplainable Code

Mitigation:

- author must review every subsystem;
- maintain research and design notes;
- document tradeoffs;
- reject code whose behavior or design cannot be independently explained.

## Risk: Weak Benchmark Claims

Mitigation:

- fixed baselines;
- saved seeds;
- public scenarios;
- report negative results;
- no cherry-picking.

## Risk: Project Never Reaches a Reproducible Release

Mitigation:

- vertical milestones;
- reproducible scenario at every stage;
- release MVP before advanced features.

## Risk: Trace Schema Drift and Ambiguous Source Semantics

Mitigation:

- own a small versioned intermediate schema;
- isolate source mappings from simulator internals;
- preserve importer/source digests and explicit warnings;
- reject unsupported versions or ambiguous required fields instead of guessing.

## Risk: Over-Fidelity Temptation

Mitigation:

- keep behavioral chunk-level fidelity as an explicit boundary;
- forbid bit-exact NCCL, RDMA, Verbs, NVLink, or GPU claims;
- require analytical tests and comparative language before added protocol detail;
- add fidelity only when a measured research question requires it.

## Risk: Dashboard Polish Delays Differentiators

Mitigation:

- schedule Cluster 13 after Cluster 24, Clusters 20–23, the initial Cluster 16 boundary, and the
  required Cluster 14 attribution path;
- use CLI reports and replay inspection for capability gates;
- treat UI as a consumer of approved schemas, never as gate evidence by itself.

## Risk: Multi-Rail State and Path Explosion

Mitigation:

- approve ADR-015 before changing topology identity;
- bound rails, attachments, candidate paths, and failure combinations;
- benchmark 512 GPUs before thresholds and measure the 2,048-GPU stretch case;
- keep one implementation cluster active and profile before optimization.

## Risk: False Hardware or Production Claims

Mitigation:

- label every result synthetic, trace-derived, or mock-shadow as applicable;
- tie public and publication claims to approved gates and immutable run receipts;
- publish adverse and neutral findings;
- state that no production control or hardware calibration is demonstrated.

## Risk: No Design Partner

Mitigation:

- continue synthetic NexusBench-v0 and public/synthetic trace work without mislabeling it adoption;
- pursue at least one external trace source or partner cluster island;
- keep Adoption Stage 3 and adoption-ready status explicitly open until held-out partner evidence
  and data-governance requirements are met.

## Risk: Scenario Overfitting and Benchmark Gaming

Mitigation:

- freeze public v0 scenarios and require version changes for behavioral edits;
- preserve multiple scenario families, seeds, failed cells, and adverse results;
- add held-out or partner scenarios for later validity work and avoid a single opaque composite score;
- record policy artifacts/configuration and reject undeclared scenario-specific dependencies.

## Risk: NexusBench Schema Instability

Mitigation:

- approve ADR-012, ADR-013, and ADR-016 before dependent implementation;
- version suite, metric, policy, trace, and result contracts independently;
- use compatibility fixtures and canonical digests;
- require a new suite version for behavior-changing scenario, seed, metric, or digest rules.

## Risk: Calibration Is Mistaken for Universal Prediction

Mitigation:

- compare policy rankings on held-out scenarios and publish disagreement/failure cases;
- scope every correlation claim to its data, topology, workload, and fidelity assumptions;
- keep predictive language forbidden until Gate 14 and ADR-018 evidence exist.

---

# 29. Security and Safety

Although NexusLab begins as a local simulator, prepare for future service exposure.

## Requirements

- validate all scenario files;
- limit scenario size;
- prevent unbounded allocations;
- sandbox external policies if added;
- avoid arbitrary code execution;
- verify replay schema;
- reject path traversal in result IDs;
- protect experiment API if deployed;
- never apply real-cluster actions by default;
- require explicit mode selection;
- log every real or advisory action.

---

# 30. Error Handling

Use typed errors where possible.

Categories:

- configuration error;
- topology error;
- scheduling error;
- route unavailable;
- resource failure;
- serialization error;
- replay error;
- policy error;
- simulation invariant violation;
- timeout;
- unsupported feature.

Requirements:

- no silent fallback for invalid experiments;
- errors include entity and run IDs;
- fatal errors preserve partial logs;
- failed runs are marked explicitly;
- UI distinguishes failed, incomplete, and successful runs.

---

# 31. Logging

Log levels:

- error;
- warning;
- info;
- debug;
- trace.

Every structured log should include when relevant:

- run ID;
- scenario ID;
- simulated time;
- event ID;
- job ID;
- flow ID;
- entity ID;
- policy name.

Do not use logs as the canonical metric store.

---

# 32. Versioning

Version:

- NexusBench suites and frozen scenario packs;
- scenario schemas;
- result schemas;
- replay schemas;
- policy implementations;
- metric definitions;
- scoring/caveat and digest contracts;
- canonical trace/telemetry intermediate schemas;
- public APIs.

Every result manifest should record:

- NexusLab version;
- git commit;
- build mode;
- scenario schema version;
- replay schema version;
- policy names and versions;
- suite/track/scenario-pack version and component digests when applicable;
- random seed;
- host information;
- timestamp.

---

# 33. Publication and Evidence Deliverables

The project is completed to produce a useful, reproducible scientific artifact. Scope and
completion are determined by the research questions, validation evidence, reproducibility, and
honesty requirements defined in this plan.

## 33.1 Evidence-Gated Public Claims

The following project or publication claims are allowed only after their named gates and evidence
exist:

- After Gate 24: published NexusBench-v0 as an open reproducible synthetic benchmark for routing
  and placement policies under frozen scenarios, metric contracts, repeats, and digests.
- After Gate 20: implemented deterministic DP/TP/PP/EP traffic planners with measured pipeline-bubble,
  collective-wait, and GPU-idle attribution.
- After Gates 21–22: modeled NVLink-domain/multi-rail behavior and replayed an identical imported
  training-step digest under alternate infrastructure policies, explicitly as behavioral simulation.
- After Gate 23: published a reproducible multi-repeat A/B study with locked inputs, immutable run
  receipts, digest verification, and model caveats.
- After Gate 16: ran one versioned policy path across simulation, replay, and mock shadow observation
  sources with auditable `applied=false` decisions.
- After Gate 14 plus ADR-018 and a completed held-out study: reported policy-ranking correlation for
  the measured domain with disagreement cases; do not generalize this to universal prediction.

No claim may substitute placeholders, manually entered values, selective seeds, or visual polish for
verified run artifacts.

## 33.2 Scientific Release Artifacts

- citable NexusLab and NexusBench versions;
- frozen scenario packs, metric dictionaries, policy contracts, and suite manifests;
- complete run receipts, component digests, failure/missing-cell records, and regeneration commands;
- NexusBench-v0 baseline results generated from saved runs;
- the first reference study under `docs/studies/<study-id>.md`;
- methods and architecture documentation sufficient for independent reproduction;
- explicit synthetic, trace-derived, calibration, and shadow/advisory limitations;
- a machine-readable result package and a human-readable technical report.

## 33.3 Technical Report Topics

- the comparability problem NexusBench addresses;
- deterministic scenario, policy, metric, repeat, and digest contracts;
- validation of the discrete-event, topology, routing, workload, and collective models;
- routing and placement baseline methodology;
- trace normalization and counterfactual replay limitations;
- advanced parallelism and heterogeneous multi-rail abstractions;
- failure semantics, attribution, and policy disagreement cases;
- what synthetic infrastructure benchmarks can and cannot establish;
- the held-out ranking-correlation protocol when Stage 3 evidence exists.

---

# 34. Definition of Done for Every Cluster

A cluster is done only when:

- code is implemented;
- interfaces are documented;
- unit tests pass;
- integration tests pass;
- sanitizers pass;
- benchmark exists;
- results are recorded;
- docs are updated;
- ADR is added if needed;
- architecture gate is completed;
- no unresolved critical questions remain;
- demo command is documented.

---

# 35. Immediate Next Steps

## Step 1

Complete Cluster 9 failure injection and recovery now that the Cluster 8 telemetry gate is approved.
Together they are the observability and failure foundation for every NexusBench track and
scientific-release capability.

## Step 2

Implement the minimum Cluster 11–12 matrix, manifest, digest, and replay-format surfaces required to
lock experiments and preserve imported-trace provenance.

## Step 3

Write and approve ADR-012 and ADR-013. Define the minimum Cluster 15 static policy SDK and Cluster 14
report boundary, then implement and gate Cluster 24 NexusBench-v0 with no result placeholders.

## Step 4

Write and approve ADR-014, then implement and gate Cluster 20 DP/TP/PP/EP workloads as the
NB-Collective foundation.

## Step 5

Write and approve ADR-015, then implement and gate Cluster 21 NVLink-domain and multi-rail fabric.

## Step 6

Write and approve ADR-016 and complete Cluster 22 trace import/replay. Run the Cluster 23 NexusBench
reference study only after real complete runs exist. Then approve ADR-017 and implement Cluster 16
mock shadow/advisory mode, followed by the deeper Cluster 14 attribution path. Cluster 13 dashboard
follows these gates. ADR-018 calibration is later Stage 3 work and cannot be implied by earlier
evidence.

---

# 36. First Planning Questions to Answer

Resolution: answered on 2026-07-17. The accepted decisions are recorded in Section 37 and `ARCHITECTURE.md`.

Before coding begins, answer these:

1. Is the simulation core definitely C++20?
2. Is Linux the only required development target initially?
3. Should Catch2 or GoogleTest be used?
4. Should YAML be the user-facing scenario format?
5. Should protobuf be introduced immediately or after MVP?
6. Should the first topology be leaf-spine or full Clos?
7. Should transfer fidelity begin at chunk level?
8. What is the first scale target: 512, 2,048, or 4,096 GPUs?
9. What is an acceptable event throughput target on your machine?
10. Should the web dashboard be in the same repository?
11. Should the first public release support only replay, not live streaming?
12. Which three policies are mandatory for MVP?
13. Which one failure scenario will anchor the demo?
14. Which metrics will appear on the landing page and README?
15. What is the maximum time budget for MVP?

---

# 37. Recommended Initial Decisions

These initial decisions were accepted on 2026-07-17. Revisit them through an ADR and architecture review rather than changing them silently.

- Language: C++20.
- Required development platform: Linux or WSL2.
- Build: CMake.
- Tests: GoogleTest and GoogleMock.
- Config: YAML through yaml-cpp.
- Replay: introduce protobuf plus compression in Cluster 12.
- Summary: JSON.
- Time: integer nanoseconds.
- Simulation: single-process discrete event.
- Transfer fidelity: chunk level.
- Topology: Clos.
- Collective: Ring AllReduce.
- Routing: ECMP, least-loaded, queue-aware.
- Scheduler: first-fit initially.
- Failure: spine-link failure.
- UI: Next.js replay dashboard.
- Storage: local filesystem plus SQLite metadata.
- First scale target: 512 simulated GPUs.
- Stretch scale target: 2,048 or more.
- First public release: replay-based, no live cluster control.
- Repository: monorepo, including the future Next.js dashboard.
- Performance thresholds: establish the baseline in Cluster 1 before defining regressions.
- Headline metrics: job completion time, GPU idle time, queue depth, link utilization, and drops.
- Scientific-release planning target: 16 weeks, with architecture gates treated as quality requirements rather than deadlines.
- License: Apache-2.0.

---

# 38. Final Project Statement

NexusLab should ultimately be described as:

> NexusLab is the open deterministic counterfactual experimentation lab for AI training infrastructure. NexusBench is its reproducible benchmark standard for comparing networking, collective, scheduling, and failure-recovery policies under frozen scenarios, metric contracts, repeats, and digests.

At scientific-release completion, that description additionally means DP/TP/PP/EP traffic, behavioral
NVLink-domain and multi-rail fabrics, approximate trace-driven replay with fixed workload digests, a
receipt-backed flagship study, and mock shadow decisions that are explicitly never applied.

Neither name implies perfect hardware fidelity, production actuation, or hyperscaler validation.

The value of NexusLab is not that it perfectly recreates a hyperscale datacenter.

The value is that it contributes a disciplined, reviewable methodology across architecture,
simulation, networking, distributed systems, observability, benchmarking, and failure analysis.

---

# 39. Project Status Tracker

| Cluster | Name | Status | Gate Approved |
|---|---|---|---|
| 0 | Project Foundation | Complete | Yes |
| 1 | Deterministic Simulation Core | Complete | Yes |
| 2 | Topology and Cluster Model | Complete | Yes |
| 3 | Link, Queue, and Transfer Model | Complete | Yes |
| 4 | Routing Policy Framework | Complete | Yes |
| 5 | Training Workload Engine | Complete | Yes |
| 6 | Collective Communication Engine | Complete | Yes |
| 7 | Scheduler and GPU Placement | Complete | Yes |
| 8 | Telemetry and Observability | Complete | Yes |
| 9 | Failure Injection and Recovery | Not Started | No |
| 10 | Congestion-Control Framework | Not Started | No |
| 11 | Experiment Orchestrator | Not Started | No |
| 12 | Result Store and Replay Format | Not Started | No |
| 13 | Web Replay Dashboard | Not Started | No |
| 14 | Analysis and Reporting | Not Started | No |
| 15 | Plugin and Extension System | Not Started | No |
| 16 | Real Telemetry and Shadow Mode | Not Started | No |
| 17 | Performance Engineering | Not Started | No |
| 18 | Testing Strategy | Not Started | No |
| 19 | Documentation | Not Started | No |
| 20 | Advanced Parallelism Workloads | Not Started | No |
| 21 | NVLink Domains and Multi-Rail Fabric | Not Started | No |
| 22 | Trace Import and Trace-Driven Replay | Not Started | No |
| 23 | Flagship Reproducible A/B Study | Not Started | No |
| 24 | NexusBench Specification and Harness | Not Started | No |

---

# 40. Weekly Review Template

```md
# NexusLab Weekly Review — YYYY-MM-DD

## Completed

- ...

## Demonstrable Output

- command:
- result:
- screenshot or recording:

## Metrics

- tests:
- coverage:
- benchmark:
- event throughput:
- memory:

## Decisions Made

- ...

## Open Questions

- ...

## Technical Debt

- ...

## Risks

- ...

## Next Cluster

- ...

## Gate Status

Proceed: YES / NO
```

---

# 41. Pull Request Template

```md
## Cluster

Cluster X — Name

## Objective

What does this PR accomplish?

## Changes

- ...

## Architecture

Which interfaces changed?

## Tests

- unit:
- integration:
- determinism:
- sanitizer:

## Benchmarks

Before:
- ...

After:
- ...

## Documentation

- ...

## Risks

- ...

## Gate Questions

- ...

## Checklist

- [ ] No unapproved scope expansion
- [ ] No silent interface changes
- [ ] Tests pass
- [ ] Sanitizers pass
- [ ] Benchmark recorded
- [ ] Docs updated
- [ ] ADR added if required
```

---

# 42. Final Release Checklist

- [ ] MVP acceptance criteria complete
- [ ] all public scenarios reproducible
- [ ] all benchmark claims verified
- [ ] no placeholder metrics
- [ ] no unreviewed generated code
- [ ] sanitizer-clean release build
- [ ] deterministic replay test passes
- [ ] README quick start tested from clean clone
- [ ] architecture and methodology diagrams exported
- [ ] limitations documented
- [ ] release tag created
- [ ] citable technical report drafted
- [ ] Gate 24 approved and NexusBench-v0 artifacts verified
- [ ] Gates 20, 21, 22, 23, and 16 approved
- [ ] ADR-012 through ADR-017 accepted
- [ ] frozen NB-Routing-v0 and NB-Placement-v0 packs each run two baselines across three declared repeats
- [ ] NexusBench metric dictionary, scoring/caveat contract, citation format, and policy guide published
- [ ] policy swap preserves frozen scenario digests and emits deterministic comparable result packages
- [ ] DP/TP/PP/EP analytical and integration suites pass
- [ ] NVLink-domain/multi-rail failure and scale suites pass
- [ ] imported-workload digest matches across alternate-policy replays
- [ ] flagship A/B study regenerated from verified run receipts with no placeholders
- [ ] mock shadow audit proves `applied=false` for every decision
- [ ] synthetic, trace-derived, behavioral-fidelity, and mock-shadow limitations are prominent
- [ ] scientific-release, research-ready, and adoption-stage claims match their actual gates
