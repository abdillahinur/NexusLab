<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab

NexusLab is a deterministic digital twin and experimentation platform for AI training infrastructure. It will model distributed workloads, collective communication, network congestion, scheduling, and failures across configurable GPU clusters so infrastructure policies can be compared through reproducible synthetic experiments.

> [!IMPORTANT]
> NexusLab has completed Clusters 0–6, including Milestone 4 — Training Workload MVP.
> Versioned synthetic scenarios now run multi-GPU jobs through compute, Ring AllReduce, and step
> barriers, with optional bucket overlap, stragglers, cancellation, and GPU idle-time metrics.
> Explicit assignments are supported; automatic placement/scheduling remains future work.
> Measurements describe this simulator on the documented host, not real-hardware fidelity.

## Current foundation

- C++20 simulation core targeting Linux and WSL2;
- CMake and Ninja build workflow;
- GoogleTest and GoogleMock 1.17.0;
- yaml-cpp 0.9.0;
- GCC and Clang CI coverage;
- AddressSanitizer and UndefinedBehaviorSanitizer configuration;
- formatting and static-analysis checks;
- a deterministic event engine with typed tracing and reproducible random-number generation;
- a validated topology-neutral graph with stable GPU, NIC, switch, rack, port, and link identity;
- deterministic direct, single-rack, leaf-spine, and Clos topology generators;
- operational failure state, shortest-path queries, canonical YAML, Graphviz DOT, and topology
  summary inspection;
- chunk-level fabric transfers with FIFO service, finite buffers, marking, and per-link counters;
- transfer progress, exactly-once final outcomes, and link/port/switch failure reconciliation;
- configuration-selected routing policies, bounded operational path caching, and decision records;
- synthetic training jobs, explicit GPU assignments, bucket overlap, and job completion/idle metrics;
- Ring AllReduce with reduce-scatter/all-gather rounds and distinct local/fabric communication;
- versioned training scenarios, phase timelines, and workload/collective benchmarks.

The [Cluster 3 gate](docs/architecture-gates/cluster-3.md) records completion evidence.
To transfer synthetic data across a generated 512-GPU Clos and observe queue buildup:

```bash
bash scripts/benchmark-transport.sh --pattern incast --flows 100
```

The output includes delivered/dropped bytes, successful/failed transfers, maximum waiting bytes,
serializer busy time, event count, and peak RSS. Run `bash scripts/benchmark-transport-suite.sh`
for the three-repeat scale and chunk-size matrix. These fixed-path benchmark harnesses remain the Milestone 2 demo.

The [Cluster 4 gate](docs/architecture-gates/cluster-4.md) records routing correctness and
comparison evidence. Compare the same synthetic traffic across policies:

```bash
for policy in shortest-path ecmp least-loaded queue-aware; do
  bash scripts/benchmark-routing.sh --policy "$policy" --pattern all-to-all --flows 1000
done
```

The harness reports successful/failed transfers, delivered/dropped bytes, successful-transfer
p50/p95 latency, queue occupancy, decision/outcome digests, cache counters, wall time, and RSS.
Use `--mode lookup --gpus 2048 --flows 10000` for cold/warm path and policy selection measurements,
or `bash scripts/benchmark-routing-suite.sh` for the complete three-repeat matrix.
Routing occurs once at transfer admission; existing transfers retain their paths and do not retry.

Run a complete training job and inspect compute/communication phases:

```bash
bash scripts/build.sh release
build/release/simulator/nexuslab train --file examples/training/two-worker.yaml --timeline
build/release/simulator/nexuslab train --file examples/training/overlap-straggler.yaml --timeline
```

[Scenario documentation](docs/training-scenarios.md) explains profiles, explicit local/fabric
parameters, failure behavior and metric definitions. [Gate 5](docs/architecture-gates/cluster-5.md)
and [Gate 6](docs/architecture-gates/cluster-6.md) record workload and collective validation.
Run `bash scripts/benchmark-training-suite.sh` for the three-repeat worker, planning, overlap,
chunk-size, concurrent-job and loss matrix. GPU assignment conflicts fail explicitly; a scheduler
will be introduced in Cluster 7.

Dependency commits are pinned in `cmake/NexusLabDependencies.cmake`.

## Prerequisites

- Linux or WSL2;
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

When the repository is accessed through a WSL mount such as `/mnt/d`, the scripts automatically place generated build files in `~/.cache/nexuslab-build`. Set `NEXUSLAB_BUILD_ROOT` to override that location. Native Linux and CI builds use the repository-local `build/` directory.

## Quick start

```bash
bash scripts/build.sh dev
bash scripts/test.sh dev
```

Run the CLI from a WSL-mounted checkout:

```bash
~/.cache/nexuslab-build/dev/simulator/nexuslab --version
~/.cache/nexuslab-build/dev/simulator/nexuslab topology summary --clos initial
~/.cache/nexuslab-build/dev/simulator/nexuslab topology summary --file topology.yaml
```

On native Linux, the equivalent path is `./build/dev/simulator/nexuslab`.

Expected version output:

```text
NexusLab 0.1.0
```

The topology summary command is inspection-only. It reports stable entity, link-type, and
operational-state counts for the approved 512-GPU initial Clos profile, the 2,048-GPU stretch
profile, or a canonical topology YAML file.

Sanitizer build:

```bash
bash scripts/build.sh sanitize
bash scripts/test.sh sanitize
```

Formatting and static analysis:

```bash
bash scripts/format.sh --check
bash scripts/lint.sh
```

Simulation-core benchmark:

```bash
bash scripts/benchmark.sh 1000000
```

The benchmark disables tracing and reports insertion and dispatch throughput, event size, current
resident memory, and peak resident memory. Cluster 1 baseline results and the post-baseline local
regression guardrails are recorded in the architecture documentation.

Topology scale benchmark:

```bash
bash scripts/benchmark-topology.sh
```

This runs fresh release processes at 64, 512, 2,048, and 8,192 GPUs and reports construction,
validation, on-demand shortest-path query, memory, and canonical YAML serialization measurements.

## Architecture and roadmap

- [Master engineering plan](NEXUSLAB_MASTER_PLAN.md)
- [Architecture](ARCHITECTURE.md)
- [Roadmap](ROADMAP.md)
- [Architecture decisions](docs/adr/)
- [Cluster 1 performance baseline](docs/benchmarks/cluster-1-baseline.md)
- [Cluster 2 topology baseline](docs/benchmarks/cluster-2-topology-baseline.md)
- [Cluster 2 validation optimization](docs/benchmarks/cluster-2-validation-optimization.md)
- [Cluster 0 architecture gate](docs/architecture-gates/cluster-0.md)
- [Cluster 1 architecture gate](docs/architecture-gates/cluster-1.md)
- [Cluster 2 architecture gate](docs/architecture-gates/cluster-2.md)
- [Contributing](CONTRIBUTING.md)

The project advances one implementation cluster at a time. Each cluster must pass its architecture gate before work begins on the next cluster.

## Initial MVP direction

- Clos topology;
- 512 GPUs as the initial scale target and 2,048 as the stretch target;
- Ring AllReduce;
- ECMP, least-loaded, and queue-aware routing;
- first-fit scheduling;
- spine-link failure scenario;
- replay-only dashboard with no live cluster control.

## Synthetic-results policy

NexusLab results will be explicitly labeled as simulated and synthetic. Comparative claims must preserve the topology, workload, seed, failure timing, simulator version, and metric definitions while changing only the policy under evaluation.

## License

Copyright 2026 NexusLab contributors.

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the complete, unmodified license text.
