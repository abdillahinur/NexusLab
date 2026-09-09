<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab

NexusLab is an open, deterministic counterfactual experimentation lab for AI training
infrastructure. NexusBench is the planned reproducible benchmark standard built on NexusLab for
comparing GPU-cluster routing, placement, collective, congestion-control, and failure-recovery
policies under frozen scenarios.

The central scientific question is comparative: when topology, workload, seed, failure timing,
simulator version, and metric definitions are held constant, how does changing one infrastructure
policy affect the modeled outcome?

> [!IMPORTANT]
> Clusters 0–7 are implemented and architecture-gate approved. Cluster 8 telemetry architecture is
> accepted, but its implementation and gate remain in progress. NexusBench-v0, trace-driven replay,
> advanced parallelism, multi-rail modeling, the reference study, and shadow/advisory mode are
> planned work—not completed capabilities or published results.

## Why NexusLab and NexusBench

Training-infrastructure policy results are difficult to compare when experiments use different
topologies, workloads, seeds, failure scripts, metrics, or fidelity assumptions. The project has two
connected roles:

- **NexusLab — the lab:** a deterministic C++20 simulation and counterfactual experimentation
  platform.
- **NexusBench — the research standard:** versioned scenario packs, policy boundaries, metric and
  repeat contracts, digests, result receipts, and citation requirements.

NexusLab favors reproducible comparative evidence over claims of perfect hardware realism.
NexusBench results must identify whether their inputs are synthetic, trace-derived, mock-shadow,
semi-real, or real.

## Current implementation

The gate-approved foundation currently provides:

- a single-process discrete-event engine using integer nanoseconds;
- deterministic event ordering, IDs, random draws, traces, and outcome digests;
- topology-neutral GPU, NIC, rack, switch, port, and directed-link identity;
- direct, single-rack, leaf-spine, and two-tier Clos topology generation;
- canonical YAML import/export, Graphviz DOT export, validation, and operational state;
- chunk-level transfers with finite FIFO queues, marking, byte accounting, and failure handling;
- shortest-path, ECMP, least-loaded, and queue-aware routing policies;
- bounded route caching, deterministic tie-breaking, and inspectable routing decisions;
- synthetic training jobs with compute phases, bucket overlap, stragglers, and cancellation;
- Ring AllReduce reduce-scatter/all-gather execution with local and fabric accounting;
- optional non-preemptive admission and GPU placement;
- first-fit, seeded-random, rack-local, and compact placement policies;
- separate scheduling wait, compute, communication, and GPU-idle measurements;
- synthetic benchmark harnesses for the simulation core, topology, transport, routing, training,
  and scheduling.

Cluster 8 is adding the versioned telemetry catalog and observation boundary needed by later
failure, result-store, and NexusBench work. The accepted design is documented in
[ADR-011](docs/adr/ADR-011-telemetry-observability-boundary.md); completion is not claimed yet.

## Explicit boundaries

NexusLab and NexusBench do not currently provide or claim:

- packet-accurate RDMA, Verbs, RoCE, InfiniBand, NCCL, NVLink, or GPU-kernel reproduction;
- prediction of real-cluster performance;
- production telemetry ingestion;
- live dashboard streaming or cluster control;
- switch programming, scheduler actuation, or automatic remediation;
- a hosted benchmark leaderboard;
- hyperscaler validation or adoption.

The first public release remains replay-only. Any future shadow/advisory path must decide without
applying actions and must record `applied=false` in every audit entry.

## Prerequisites

The required development environment is Linux or WSL2 with:

- CMake 3.20 or newer;
- Ninja;
- GCC or Clang with C++20 support;
- Git;
- `clang-format` and `clang-tidy` for quality checks.

On Ubuntu or WSL2 Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang clang-format clang-tidy cmake git ninja-build
```

GoogleTest/GoogleMock 1.17.0 and yaml-cpp 0.9.0 are pinned by the CMake dependency configuration.
Protobuf remains deferred until Cluster 12.

## Build and test

From the repository root:

```bash
bash scripts/build.sh dev
bash scripts/test.sh dev
```

Sanitizer validation:

```bash
bash scripts/build.sh sanitize
bash scripts/test.sh sanitize
```

Formatting and static analysis:

```bash
bash scripts/format.sh --check
bash scripts/lint.sh
```

When the checkout is under a WSL mount such as `/mnt/d`, scripts place generated build files under
`~/.cache/nexuslab-build`. Set `NEXUSLAB_BUILD_ROOT` to select another build location. Native Linux
and CI use the repository-local `build/` directory.

## Run the current CLI

Build the release configuration:

```bash
bash scripts/build.sh release
```

On a WSL-mounted checkout:

```bash
~/.cache/nexuslab-build/release/simulator/nexuslab --version
~/.cache/nexuslab-build/release/simulator/nexuslab topology summary --clos initial
~/.cache/nexuslab-build/release/simulator/nexuslab topology summary --clos stretch
~/.cache/nexuslab-build/release/simulator/nexuslab train \
  --file examples/training/two-worker.yaml --timeline
~/.cache/nexuslab-build/release/simulator/nexuslab train \
  --file examples/training/scheduled.yaml --timeline
```

On native Linux, replace `~/.cache/nexuslab-build/release` with `build/release`.

The initial Clos profile contains 512 simulated GPUs. The stretch profile contains 2,048 simulated
GPUs. These are model scales, not real-cluster performance claims.

## Current benchmark harnesses

All current benchmark outputs are synthetic and describe this simulator on the documented host.

```bash
# Simulation-core event scheduling
bash scripts/benchmark.sh 1000000

# Topology construction, validation, path lookup, memory, and serialization
bash scripts/benchmark-topology.sh

# Transport queueing, incast, all-to-all, chunk-size, and scale cases
bash scripts/benchmark-transport.sh --pattern incast --flows 100
bash scripts/benchmark-transport-suite.sh

# Routing policies under identical synthetic traffic
bash scripts/benchmark-routing.sh --policy ecmp --pattern all-to-all --flows 1000
bash scripts/benchmark-routing.sh --policy queue-aware --pattern all-to-all --flows 1000
bash scripts/benchmark-routing-suite.sh

# Training workload and Ring AllReduce cases
bash scripts/benchmark-training-suite.sh

# Placement under rack pressure and other admission cases
bash scripts/benchmark-scheduling.sh --policy compact --case rack-pressure
bash scripts/benchmark-scheduling-suite.sh
```

Recorded baseline documents preserve commands, environments, limitations, and measured results.
They should not be generalized beyond their declared synthetic configurations.

## NexusBench research program

NexusBench is planned and has no published benchmark pack or result ranking yet. Its specification
outline is in [docs/nexusbench/README.md](docs/nexusbench/README.md).

NexusBench-v0 will initially include:

- **NB-Routing-v0:** ECMP versus queue-aware routing under frozen incast and spine-link-failure
  scenarios.
- **NB-Placement-v0:** first-fit versus compact placement under frozen rack-pressure and
  multi-tenant scenarios.

The v0 acceptance bar requires:

- frozen, versioned scenario packs checked into the repository;
- at least two baseline policies for every included track;
- exactly three declared repeats for each required comparison cell;
- a published metric dictionary and failed/missing-cell rules;
- deterministic suite, scenario, seed, policy, simulator, metric-catalog, and outcome digests;
- a documented static C++20 policy submission boundary;
- repository-native machine-readable and Markdown result formats;
- clean-clone reproduction and citation instructions;
- prominent synthetic-result labels and no fabricated measurements.

Later gated tracks add DP/TP/PP/EP workloads, heterogeneous multi-rail fabrics, degraded-fabric
recovery, trace-driven counterfactuals, and eventually held-out policy-ranking calibration.

## Scientific release path

The project uses evidence gates rather than presentation milestones:

1. **Thin MVP:** Clos, Ring AllReduce, routing, placement, failure, metrics, persistence, and CLI
   replay.
2. **NexusBench research-ready:** v0 specification, frozen packs, baselines, repeat harness, metric
   contract, digests, and reproducible artifacts.
3. **Scientific release complete:** NexusBench-v0 plus advanced parallelism, multi-rail modeling,
   trace counterfactuals, one measured reference study, and the non-applied shadow boundary.
4. **Adoption Stage 3 trust evidence:** a design-partner or real/semi-real held-out study comparing
   policy rankings and reporting disagreement cases.

The 16-week figure in the plan is a planning target only. Architecture gates, validation evidence,
and reproducibility determine completion.

## Reproducibility and claims

Every controlled comparison must hold constant all declared non-policy inputs. Results must preserve
scenario and policy versions, seeds, metric definitions, manifests, and digests. Negative, neutral,
failed, and incomplete runs must not be hidden.

Allowed claims are scoped to the evidence, for example:

> Under the documented synthetic workload model and locked scenario, Policy A changed simulated job
> completion time relative to Policy B.

Until calibration exists, the project must not claim that simulated values predict real cluster
performance or match NCCL/RDMA behavior.

## Documentation

- [Master engineering plan](NEXUSLAB_MASTER_PLAN.md) — source of truth
- [Architecture](ARCHITECTURE.md)
- [Roadmap](ROADMAP.md)
- [NexusBench planning specification](docs/nexusbench/README.md)
- [Study artifact rules](docs/studies/README.md)
- [Training scenario guide](docs/training-scenarios.md)
- [Scheduling guide](docs/scheduling.md)
- [Architecture decision records](docs/adr/)
- [Architecture gates](docs/architecture-gates/)
- [Recorded benchmarks](docs/benchmarks/)
- [Contributing](CONTRIBUTING.md)

The master plan owns future scope and status. Architecture documents describe accepted boundaries;
gate documents record the evidence for completed clusters.

## License

Copyright 2026 NexusLab contributors.

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the complete, unmodified
license text.
