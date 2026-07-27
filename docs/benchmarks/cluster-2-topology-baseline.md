<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 2 Topology Baseline

## Scope

This document records the first performance, memory, and serialization baseline for the Cluster 2
topology model. It is a local engineering reference, not a hardware-neutral performance claim.
Regression thresholds are intentionally not defined from this first measurement set.

The benchmark generates and validates the canonical Clos configuration with eight GPUs per NIC,
eight NICs per leaf, and eight spines at 64, 512, 2,048, and 8,192 GPUs.

## Measured revision and environment

- Revision: `153d86ddb2f53fb167c81d47ede1f9440f73dde1`
- Measured: 2026-07-27 starting at 23:25 UTC
- Build: CMake `Release`, GCC 13.3.0
- Platform: Ubuntu 24.04.4 LTS on WSL2
- Kernel: Linux 6.18.33.2-microsoft-standard-WSL2, x86-64
- CPU: AMD Ryzen 9 5900X 12-Core Processor, 24 logical CPUs exposed
- WSL2 memory: 16,334,520 KiB
- CMake: 3.28.3
- Ninja: 1.11.1

The workstation was not CPU-pinned or isolated from background activity. Results should therefore be
compared using the same environment and multiple fresh processes.

## Method

Each scale was measured in three fresh release processes. The summary values are the median of the
three independent runs.

- Construction covers canonical Clos graph generation.
- Validation covers generic structural validation plus Clos profile validation.
- The representative path query runs from GPU 0 to the last GPU in the generated topology.
- Shortest paths currently use on-demand BFS. There is no preprocessing or route cache, so the
  reported preprocessing time is zero by definition and is not a measured performance result.
- Serialization covers the public canonical YAML export operation, including its internal generic
  validation pass.
- Current and peak RSS come from `/proc/self/status`.
- Construction RSS delta is measured immediately after graph construction and before validation.
- Peak RSS includes validation, path lookup, and YAML serialization.

Equivalent command:

```bash
bash scripts/benchmark-topology.sh
```

## Median results

| GPUs | Construction | Validation | Path query | YAML serialization | YAML size | Construction RSS delta | Peak RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 29,585 ns | 21,921 ns | 2,805 ns | 2,652,926 ns | 25,991 bytes | 208 KiB | 3,940 KiB |
| 512 | 142,587 ns | 584,533 ns | 20,628 ns | 20,895,761 ns | 210,685 bytes | 408 KiB | 4,680 KiB |
| 2,048 | 719,282 ns | 7,309,970 ns | 113,943 ns | 86,279,283 ns | 864,019 bytes | 928 KiB | 7,136 KiB |
| 8,192 | 2,938,074 ns | 121,633,806 ns | 466,793 ns | 453,489,006 ns | 3,525,747 bytes | 3,108 KiB | 16,968 KiB |

The representative 64-GPU topology has one leaf, so its GPU-to-GPU path is four hops with one path.
The larger scales select GPUs on different leaves and consistently report six hops with eight
equal-cost paths.

## Raw runs

### 64 GPUs

| Run | Construction ns | Validation ns | Path query ns | Serialization ns | RSS construction delta KiB | RSS after serialization KiB | Peak RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 26,760 | 19,817 | 2,805 | 2,403,757 | 200 | 3,936 | 3,936 |
| 2 | 29,585 | 21,921 | 2,665 | 2,652,926 | 224 | 3,940 | 3,940 |
| 3 | 37,917 | 33,248 | 4,377 | 3,891,653 | 208 | 3,940 | 3,940 |

Every run generated 8 NICs, 1 leaf, 8 spines, 160 ports, 80 links, and 25,991 bytes of YAML.

### 512 GPUs

| Run | Construction ns | Validation ns | Path query ns | Serialization ns | RSS construction delta KiB | RSS after serialization KiB | Peak RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 142,587 | 584,533 | 20,628 | 20,052,640 | 408 | 4,324 | 4,644 |
| 2 | 177,969 | 596,544 | 24,516 | 20,895,761 | 408 | 4,360 | 4,680 |
| 3 | 139,598 | 473,509 | 19,504 | 21,646,121 | 392 | 4,360 | 4,680 |

Every run generated 64 NICs, 8 leaves, 8 spines, 1,280 ports, 640 links, and 210,685 bytes of YAML.

### 2,048 GPUs

| Run | Construction ns | Validation ns | Path query ns | Serialization ns | RSS construction delta KiB | RSS after serialization KiB | Peak RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1,098,282 | 7,571,219 | 113,943 | 84,198,035 | 944 | 5,600 | 7,096 |
| 2 | 719,282 | 7,309,970 | 160,457 | 102,019,054 | 928 | 5,580 | 7,140 |
| 3 | 613,807 | 7,182,080 | 76,385 | 86,279,283 | 928 | 5,640 | 7,136 |

Every run generated 256 NICs, 32 leaves, 8 spines, 5,120 ports, 2,560 links, and 864,019 bytes of
YAML.

### 8,192 GPUs

| Run | Construction ns | Validation ns | Path query ns | Serialization ns | RSS construction delta KiB | RSS after serialization KiB | Peak RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 3,388,370 | 121,633,806 | 338,472 | 441,429,599 | 3,124 | 10,308 | 16,976 |
| 2 | 2,938,074 | 114,557,803 | 466,793 | 453,489,006 | 3,108 | 10,300 | 16,968 |
| 3 | 2,552,530 | 146,506,760 | 536,150 | 468,662,776 | 3,108 | 10,256 | 16,924 |

Every run generated 1,024 NICs, 128 leaves, 8 spines, 20,480 ports, 10,240 links, and 3,525,747
bytes of YAML.

## Baseline interpretation

- All required scales generate, validate, route, and serialize successfully in fresh processes.
- Construction time and construction RSS growth remain modest at the 8,192-GPU evidence scale.
- Canonical YAML size grows predictably with entity and link count and reaches about 3.53 MB at
  8,192 GPUs.
- On-demand representative path lookup remains below one millisecond at every measured scale.
- Validation grows materially faster than topology size. The generic validator repeatedly scans
  links while checking port occupancy, making validation the clearest measured optimization
  candidate.
- Serialization is the largest wall-clock phase and includes another generic validation pass.
- Peak RSS at 8,192 GPUs is about 16.6 MiB and includes the in-memory YAML emitter and output string.

These observations justify profiling and optimizing validation before Architecture Gate 2. Any
optimization must preserve structured errors, deterministic behavior, and the complete correctness
matrix. This baseline supplies the before measurement; it does not itself authorize regression
thresholds or public performance claims.
