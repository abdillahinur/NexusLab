<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab — Master Engineering Plan

> **Working title:** NexusLab  
> **Tagline:** A digital twin and experimentation platform for AI training infrastructure  
> **Primary goal:** Simulate, observe, compare, and eventually advise on the behavior of large-scale GPU training clusters using reproducible synthetic workloads and pluggable infrastructure policies.

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
- make the finished project explainable in senior software engineering interviews.

This is a living document. Any major change to architecture, scope, or interfaces must be reflected here before implementation continues.

---

# 1. Executive Summary

NexusLab is a digital twin of an AI training cluster.

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

The first release uses fully simulated data.

Later versions may:

- import real GPU, NCCL, scheduler, and switch telemetry;
- replay real cluster incidents;
- operate in shadow mode beside a real cluster;
- produce advisory recommendations;
- integrate with Kubernetes, Slurm, NCCL configuration, or network controllers.

---

# 2. Project Positioning

## 2.1 One-Sentence Description

NexusLab is a deterministic distributed-systems simulation platform for testing AI cluster scheduling, communication, routing, congestion, and failure-recovery policies before they are applied to real infrastructure.

## 2.2 Thirty-Second Pitch

NexusLab creates a configurable virtual GPU cluster and runs synthetic distributed-training workloads across it. Engineers can compare routing algorithms, collective strategies, congestion controllers, scheduling policies, and failure scenarios using identical seeded experiments. NexusLab records full telemetry, produces reproducible benchmarks, and provides a visual replay dashboard explaining why jobs slowed down and how each policy responded.

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

## 3.2 Portfolio Goal

NexusLab should demonstrate that the author can:

- design a complex system from first principles;
- define clean abstractions;
- reason about distributed systems;
- work with graph algorithms and scheduling;
- build performance-sensitive C++;
- implement deterministic simulation;
- create measurable benchmarks;
- design telemetry and observability systems;
- communicate architectural tradeoffs;
- integrate a systems backend with a polished frontend;
- validate correctness rather than relying on visual output;
- plan for real-world integration without pretending the simulation is production.

## 3.3 Senior SWE Signal

The completed project should provide evidence of:

- system ownership;
- architectural judgment;
- technical depth;
- performance analysis;
- extensibility;
- failure handling;
- test strategy;
- operational thinking;
- documentation quality;
- ability to lead AI coding agents instead of merely accepting generated code.

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
15. document assumptions and limitations honestly.

The project reaches portfolio quality when an external engineer can clone the repository, run an example scenario, compare policies, replay the result, and understand the architecture without assistance.

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
- JSON or protobuf output;
- CLI benchmark runner;
- web replay dashboard;
- core metrics and policy comparison.

## 6.2 Version 2 Scope

Version 2 may include:

- Tree AllReduce;
- hierarchical AllReduce;
- compute/communication overlap;
- gradient buckets;
- ECN-style signals;
- delay-based congestion control;
- flowlet routing;
- topology-aware scheduling;
- checkpoint traffic;
- experiment database;
- decision explanation UI;
- scenario editor;
- trace import;
- pluggable Python policies.

## 6.3 Version 3 Scope

Version 3 may include:

- real DCGM telemetry adapters;
- NCCL trace import;
- Kubernetes or Slurm scheduler adapter;
- shadow-mode policy evaluation;
- advisory recommendations;
- emulated worker processes;
- network namespace test environment;
- multi-node NexusLab execution;
- real-cluster configuration recommendations;
- safety and rollback framework.

## 6.4 Explicit Non-Goals for Initial Release

Do not build these before the MVP is complete:

- full RDMA protocol simulation;
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

---

# 7. Primary Users

## 7.1 Infrastructure Engineer

Wants to compare network or scheduling policies.

## 7.2 Researcher

Wants deterministic experiments and reproducible results.

## 7.3 Platform Engineer

Wants to understand how workload placement affects cluster performance.

## 7.4 Student or Portfolio Reviewer

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
│   └── interview-notes/
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

Only the policy under evaluation should change.

## 16.2 Repeated Runs

For stochastic scenarios:

- use a fixed seed set;
- report mean and variance;
- preserve every seed;
- avoid selecting only favorable runs.

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

## Interface

```cpp
struct RouteRequest {
    FlowId flow_id;
    EntityId source;
    EntityId destination;
    std::uint64_t bytes_remaining;
    FabricSnapshot snapshot;
};

struct RouteDecision {
    Path path;
    std::string reason;
    double confidence;
};

class RoutingPolicy {
public:
    virtual RouteDecision choose_route(
        const RouteRequest& request
    ) = 0;

    virtual ~RoutingPolicy() = default;
};
```

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
- policy decisions are inspectable.

## Checkpoint Questions

- Are we recording too much?
- Can summaries be reproduced from traces?
- Which metrics are essential for portfolio demos?
- Are units explicit?
- Are timestamps aligned across subsystems?

## Architecture Gate 8

Approve telemetry schemas before building replay and dashboard features.

---

# Cluster 9 — Failure Injection and Recovery

## Objective

Model predictable failures and measure policy response.

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
- dashboard can display failure timelines.

## Checkpoint Questions

- Are failures observed immediately?
- Is delayed detection necessary for meaningful controller evaluation?
- How are in-flight bytes handled?
- Is retry modeled?
- What behavior is documented as simplified?

## Architecture Gate 9

Review failure semantics before comparing recovery policies.

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

## Questions

- Is orchestration a CLI process or service?
- How are runs identified?
- How are policy versions captured?
- How are failures retried?
- Can runs execute in parallel?
- How are resource limits enforced?
- How are partial matrices resumed?

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
- resume support;
- failure reporting.

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
- resume behavior;
- duplicate prevention;
- seed generation;
- result manifest correctness.

## Acceptance Criteria

- one command runs a comparison suite;
- every run records full provenance;
- interrupted matrices can resume;
- failures do not corrupt successful runs;
- results are easy to locate.

## Architecture Gate 11

Review whether orchestration belongs in C++ or a separate service before scaling run management.

---

# Cluster 12 — Result Store and Replay Format

## Objective

Persist experiment summaries and event traces for later analysis and visualization.

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

## Architecture Gate 12

Approve replay format before the frontend depends on it.

---

# Cluster 13 — Web Replay Dashboard

## Objective

Build a polished interface that explains cluster behavior and policy differences.

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
- decisions can be inspected.

## Architecture Gate 13

Review whether the UI explains the system or merely decorates it.

---

# Cluster 14 — Analysis and Reporting

## Objective

Generate trustworthy benchmark reports and portfolio-ready results.

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

## Acceptance Criteria

- reports are reproducible from saved results;
- every chart links to scenario definitions;
- no manual spreadsheet editing is required;
- negative or neutral results are preserved;
- claims include limitations.

---

# Cluster 15 — Plugin and Extension System

## Objective

Allow new policies and models to be added without modifying the simulation core.

## Questions

- Compile-time C++ plugins or runtime shared libraries?
- Is a Python policy API worth the complexity?
- How are plugin versions recorded?
- How are unsafe plugins isolated?
- Which state is exposed?
- How is backward compatibility managed?

## MVP

Use statically registered C++ policy implementations.

## Later

Consider:

- shared-library plugins;
- Python policy process via gRPC;
- policy SDK;
- sandboxed policy runner.

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
- sample plugin is documented.

---

# Cluster 16 — Real Telemetry and Shadow Mode

## Objective

Prepare NexusLab for eventual connection to real GPU-cluster data without compromising simulation architecture.

## Modes

### Simulation Mode

All state is generated internally.

### Replay Mode

State is reconstructed from recorded telemetry.

### Shadow Mode

NexusLab observes a real cluster and generates decisions that are not applied.

### Advisory Mode

NexusLab produces human-reviewed recommendations.

### Controlled Mode

NexusLab applies limited approved actions with safety controls.

## Backend Interface

```cpp
class FabricBackend {
public:
    virtual FabricSnapshot read_state() = 0;
    virtual ActionResult apply(const FabricAction& action) = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual ~FabricBackend() = default;
};
```

## Potential Adapters

- simulated backend;
- replay backend;
- DCGM telemetry adapter;
- Prometheus adapter;
- NCCL trace adapter;
- Kubernetes scheduler adapter;
- Slurm adapter;
- switch telemetry adapter.

## Safety Requirements

Before any real action:

- allowlist actions;
- confidence threshold;
- dry-run mode;
- rate limits;
- audit log;
- rollback plan;
- canary scope;
- human approval;
- fallback policy;
- timeout behavior.

## Acceptance Criteria for Initial Project

The real-cluster adapter does not need to be fully implemented.

The project should include:

- backend interface;
- documented mapping from real telemetry;
- shadow-mode architecture;
- mock live adapter;
- clear production-safety limitations.

---

# Cluster 17 — Performance Engineering

## Objective

Demonstrate deliberate systems performance work.

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
- millions of events per second in release mode;
- deterministic replay hashes;
- manageable memory usage;
- parallel experiment execution across CPU cores.

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

## Initial ADR List

- ADR-001: Use discrete-event simulation.
- ADR-002: Use integer nanoseconds for simulated time.
- ADR-003: Use chunk-level rather than packet-level transfers.
- ADR-004: Begin with Clos topology.
- ADR-005: Separate policies from simulation core.
- ADR-006: Use YAML scenarios.
- ADR-007: Use deterministic seeded randomness.
- ADR-008: Separate collective planning from routing.
- ADR-009: Store replay independently from the simulator.
- ADR-010: Treat real-cluster support as a backend abstraction.
- ADR-011: Use C++ for the simulation core.
- ADR-012: Use React or Next.js for replay visualization.
- ADR-013: Record policy decision explanations.
- ADR-014: Optimize job completion time rather than only flow latency.

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
- Can I defend the design in an interview?

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

Includes:

- Clusters 5 and 6;
- jobs;
- Ring AllReduce;
- completion time.

Demo:

- run a multi-GPU training job;
- show compute and communication phases.

## Milestone 5 — Multi-Tenant Cluster

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

## Milestone 8 — Portfolio Dashboard

Includes:

- Cluster 13.

Demo:

- polished web replay;
- comparison and decision inspector.

## Milestone 9 — Advanced Controllers

Includes:

- Cluster 10;
- advanced collective planning;
- more scheduling policies.

## Milestone 10 — Real-Cluster Readiness

Includes:

- Cluster 16;
- mock telemetry adapter;
- shadow-mode architecture.

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
- results can be replayed in the dashboard;
- the README contains a complete reproducible demo.

Anything beyond this is not required for the first public release.

---

# 25. Portfolio Release Definition

The project is portfolio-ready when it includes:

- clean public repository;
- polished README;
- architecture diagram;
- one-click or simple local demo;
- short demo video;
- three benchmark reports;
- failure replay;
- decision inspector;
- honest limitations;
- clear setup;
- no fake metrics;
- tagged release;
- resume bullets;
- technical write-up.

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

---

# 28. Risks and Mitigations

## Risk: Scope Explosion

Mitigation:

- MVP definition;
- cluster gates;
- non-goal list;
- release milestones.

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
- maintain interview notes;
- document tradeoffs;
- reject code that cannot be defended.

## Risk: Weak Benchmark Claims

Mitigation:

- fixed baselines;
- saved seeds;
- public scenarios;
- report negative results;
- no cherry-picking.

## Risk: Project Never Reaches Demo Quality

Mitigation:

- vertical milestones;
- demo scenario at every stage;
- release MVP before advanced features.

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

- scenario schemas;
- result schemas;
- replay schemas;
- policy implementations;
- metric definitions;
- public APIs.

Every result manifest should record:

- NexusLab version;
- git commit;
- build mode;
- scenario schema version;
- replay schema version;
- policy names and versions;
- random seed;
- host information;
- timestamp.

---

# 33. Resume and Interview Deliverables

## Resume Bullet Examples

Replace placeholders with measured results.

- Built **NexusLab**, a C++20 discrete-event digital twin for multi-tenant AI training clusters, modeling configurable Clos fabrics, distributed collective communication, congestion, failures, and GPU scheduling across up to **X simulated workers**.
- Designed pluggable routing and placement policy interfaces and executed **Y deterministic experiments**, reducing simulated p99 job completion time by **Z%** versus ECMP under documented synthetic workloads.
- Implemented a replay and observability pipeline that correlated queue growth, link failures, collective stalls, and GPU idle time through an interactive TypeScript dashboard.
- Profiled and optimized event scheduling, route lookup, and telemetry serialization, improving simulation throughput from **A to B events per second** while preserving deterministic output.

## Interview Topics to Prepare

- why discrete-event simulation;
- packet versus chunk versus flow abstraction;
- deterministic ordering;
- topology representation;
- Ring AllReduce mechanics;
- route-selection tradeoffs;
- scheduler locality;
- metrics and baseline design;
- event volume and memory;
- telemetry overhead;
- replay schema;
- failure semantics;
- real-cluster integration;
- limitations;
- what you would redesign.

## Technical Write-Up Ideas

- Building a deterministic simulator for AI clusters;
- Why job completion time matters more than flow latency;
- Simulating Ring AllReduce over Clos networks;
- How queue-aware routing can help or hurt;
- Designing replayable distributed-systems experiments;
- What synthetic infrastructure benchmarks can and cannot prove.

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

Create repository and commit:

- this master plan;
- README skeleton;
- architecture skeleton;
- ADR template;
- roadmap;
- CMake foundation.

## Step 2

Resolve Cluster 0 questions.

## Step 3

Write ADR-001 through ADR-003:

- discrete-event simulation;
- integer simulated time;
- chunk-level transfer model.

## Step 4

Implement Cluster 1 only.

## Step 5

Run Architecture Gate 1 before any topology implementation.

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
- Portfolio-ready MVP target: 16 weeks, with architecture gates treated as quality requirements rather than deadlines.
- License: Apache-2.0.

---

# 38. Final Project Statement

NexusLab should ultimately be described as:

> NexusLab is a deterministic digital twin and experimentation platform for AI training infrastructure. It models distributed workloads, collective communication, network congestion, scheduling, and failures across configurable GPU clusters. Engineers can compare policies through reproducible experiments, inspect why jobs slowed down, replay incidents, and eventually evaluate recommendations against real cluster telemetry.

The value of NexusLab is not that it perfectly recreates a hyperscale datacenter.

The value is that it demonstrates disciplined engineering across architecture, simulation, networking, distributed systems, observability, benchmarking, failure handling, and product-quality visualization.

---

# 39. Project Status Tracker

| Cluster | Name | Status | Gate Approved |
|---|---|---|---|
| 0 | Project Foundation | Complete | Yes |
| 1 | Deterministic Simulation Core | Complete | Yes |
| 2 | Topology and Cluster Model | Not Started | No |
| 3 | Link, Queue, and Transfer Model | Not Started | No |
| 4 | Routing Policy Framework | Not Started | No |
| 5 | Training Workload Engine | Not Started | No |
| 6 | Collective Communication Engine | Not Started | No |
| 7 | Scheduler and GPU Placement | Not Started | No |
| 8 | Telemetry and Observability | Not Started | No |
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
- [ ] demo video recorded
- [ ] architecture diagram exported
- [ ] limitations documented
- [ ] release tag created
- [ ] resume bullets updated with real values
- [ ] technical article drafted
- [ ] project page added to portfolio
