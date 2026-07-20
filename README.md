<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab

NexusLab is a deterministic digital twin and experimentation platform for AI training infrastructure. It will model distributed workloads, collective communication, network congestion, scheduling, and failures across configurable GPU clusters so infrastructure policies can be compared through reproducible synthetic experiments.

> [!IMPORTANT]
> NexusLab is in Cluster 0 (Project Foundation). It does not yet simulate training clusters, and no performance or realism claims are made at this stage.

## Current foundation

- C++20 simulation core targeting Linux and WSL2;
- CMake and Ninja build workflow;
- GoogleTest and GoogleMock 1.17.0;
- yaml-cpp 0.9.0;
- GCC and Clang CI coverage;
- AddressSanitizer and UndefinedBehaviorSanitizer configuration;
- formatting and static-analysis checks;
- a lightweight benchmark-harness smoke test.

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
```

On native Linux, the equivalent path is `./build/dev/simulator/nexuslab`.

Expected version output:

```text
NexusLab 0.1.0
```

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

Benchmark-harness smoke test:

```bash
bash scripts/benchmark.sh 1000000
```

This benchmark validates the benchmark executable and reporting path only. Simulation performance baselines begin in Cluster 1.

## Architecture and roadmap

- [Master engineering plan](NEXUSLAB_MASTER_PLAN.md)
- [Architecture](ARCHITECTURE.md)
- [Roadmap](ROADMAP.md)
- [Architecture decisions](docs/adr/)
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
