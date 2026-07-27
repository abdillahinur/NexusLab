<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 2 Validation Optimization

## Scope

This document records the measured optimization of topology port-occupancy validation. The change
was selected from the Cluster 2 baseline evidence rather than from an assumed bottleneck.

- Before revision: `153d86ddb2f53fb167c81d47ede1f9440f73dde1`
- Optimized revision: `8fa91ea87db917828a99f74f5d37b7de8ef56bba`
- Optimized measurement: 2026-07-27 starting before 23:34 UTC
- Environment and benchmark method: unchanged from the
  [Cluster 2 topology baseline](cluster-2-topology-baseline.md)

Both revisions use a CMake `Release` build with GCC 13.3.0 on the documented Ubuntu 24.04 WSL2
reference environment. Each summary value is the median of three fresh processes.

## Change

The baseline validator checked each port by scanning every physical link and counting matching
endpoints. With `P` ports and `L` links, port occupancy therefore cost \(O(P \times L)\).

The optimized validator:

1. allocates one saturating byte per port;
2. visits every link endpoint once and records zero, one, or multiple uses;
3. visits every port once to emit the existing `PortLinkCountMismatch` error when its count is not
   exactly one.

This changes port-occupancy validation to \(O(P + L)\). Saturating at two preserves all information
needed by the validator while bounding temporary storage to one byte per port. At 8,192 GPUs, that
temporary occupancy vector contains 20,480 bytes.

The optimization does not alter graph construction, identity, adjacency, routing, serialization
format, or operational-state behavior. Dedicated tests corrupt link endpoints after valid
construction and verify that unlinked, multiply linked, and unknown endpoint cases retain structured
errors without indexing outside the port vector.

## Before and after medians

### Validation

| GPUs | Baseline validation | Optimized validation | Speedup | Time reduction |
|---:|---:|---:|---:|---:|
| 64 | 21,921 ns | 12,810 ns | 1.71× | 41.6% |
| 512 | 584,533 ns | 70,506 ns | 8.29× | 87.9% |
| 2,048 | 7,309,970 ns | 280,578 ns | 26.05× | 96.2% |
| 8,192 | 121,633,806 ns | 1,390,793 ns | 87.46× | 98.9% |

### Canonical YAML serialization

The public serialization operation performs generic validation before emitting YAML, so it also
benefits from the occupancy optimization.

| GPUs | Baseline serialization | Optimized serialization | Speedup | Time reduction |
|---:|---:|---:|---:|---:|
| 64 | 2,652,926 ns | 2,392,726 ns | 1.11× | 9.8% |
| 512 | 20,895,761 ns | 19,262,155 ns | 1.08× | 7.8% |
| 2,048 | 86,279,283 ns | 75,175,078 ns | 1.15× | 12.9% |
| 8,192 | 453,489,006 ns | 318,295,621 ns | 1.42× | 29.8% |

Serialization bytes are unchanged at every scale.

## Optimized median results

| GPUs | Construction | Validation | Path query | YAML serialization | Construction RSS delta | Peak RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 64 | 26,810 ns | 12,810 ns | 2,659 ns | 2,392,726 ns | 224 KiB | 3,928 KiB |
| 512 | 136,156 ns | 70,506 ns | 16,866 ns | 19,262,155 ns | 392 KiB | 4,680 KiB |
| 2,048 | 519,846 ns | 280,578 ns | 77,108 ns | 75,175,078 ns | 928 KiB | 7,132 KiB |
| 8,192 | 2,600,988 ns | 1,390,793 ns | 321,994 ns | 318,295,621 ns | 3,108 KiB | 16,968 KiB |

Construction, path-query, and memory values remain within the normal run-to-run variation visible
in the baseline. Canonical YAML sizes remain 25,991, 210,685, 864,019, and 3,525,747 bytes
respectively.

## Optimized raw runs

| GPUs | Run | Validation ns | Serialization ns | Construction ns | Path query ns | RSS construction delta KiB | Peak RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 1 | 13,589 | 2,392,726 | 26,755 | 2,609 | 224 | 3,936 |
| 64 | 2 | 11,057 | 2,291,268 | 26,810 | 2,659 | 224 | 3,904 |
| 64 | 3 | 12,810 | 3,299,293 | 34,838 | 2,738 | 220 | 3,928 |
| 512 | 1 | 94,596 | 19,262,155 | 247,257 | 24,899 | 376 | 4,636 |
| 512 | 2 | 70,506 | 19,439,243 | 136,156 | 16,866 | 392 | 4,688 |
| 512 | 3 | 58,932 | 18,084,368 | 131,005 | 16,180 | 392 | 4,680 |
| 2,048 | 1 | 280,578 | 73,668,733 | 519,846 | 71,142 | 916 | 7,096 |
| 2,048 | 2 | 292,559 | 75,175,078 | 510,327 | 95,771 | 928 | 7,136 |
| 2,048 | 3 | 260,995 | 76,148,692 | 567,383 | 77,108 | 928 | 7,132 |
| 8,192 | 1 | 1,513,272 | 330,989,388 | 2,720,187 | 352,451 | 3,108 | 16,844 |
| 8,192 | 2 | 1,193,424 | 294,859,769 | 2,236,565 | 294,737 | 3,108 | 16,968 |
| 8,192 | 3 | 1,390,793 | 318,295,621 | 2,600,988 | 321,994 | 3,108 | 16,988 |

## Interpretation

- The speedup increases with topology size, matching the intended complexity improvement.
- At 8,192 GPUs, validation falls from the dominant 121.6 ms phase to 1.39 ms, below construction
  time and far below canonical YAML emission.
- Serialization remains the dominant phase because formatting and emitting millions of bytes now
  outweigh validation.
- The temporary occupancy vector has no material effect on measured process memory.
- The full 103-test GCC, Clang, ASan, and UBSan matrix passes, including new malformed-occupancy
  coverage.

The measurement supports keeping the simple contiguous occupancy vector. More complex validation
data structures are not justified. Regression thresholds remain an Architecture Gate 2 decision and
must account for normal workstation variance rather than using the fastest individual run.
