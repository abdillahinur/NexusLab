<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 1 Simulation-Core Baseline

## Scope

This document records the first measured performance and memory baseline for the deterministic
simulation core. It is a local engineering reference, not a hardware-neutral performance claim.
Regression thresholds are intentionally not defined in this baseline.

The benchmark exercises engine-assigned event IDs, queue insertion, pending-ID tracking, typed
dispatch, simulated-clock advancement, and result accounting. Trace collection is disabled.

## Measured revision and environment

- Revision: `0a04cb4f4b66fcfd9e1ca37714b3d4021d30926f`
- Measured: 2026-07-25 at 06:00 UTC
- Build: CMake `Release`, GCC 13.3.0
- Platform: Ubuntu 24.04.4 LTS on WSL2
- Kernel: Linux 6.18.33.2-microsoft-standard-WSL2, x86-64
- CPU: AMD Ryzen 9 5900X 12-Core Processor, 24 logical CPUs exposed
- WSL2 memory: 16,334,528 KiB
- CMake: 3.28.3
- Ninja: 1.11.1

The workstation was not CPU-pinned or isolated from background activity. Results should therefore be
compared using the same environment and multiple runs.

## Method

Each scale was measured in three fresh processes using the release benchmark executable. Every event
used normal priority, a timestamp increasing from zero in one-nanosecond steps, and a `NoOpEvent`
token equal to its insertion index. The simulation seed was 42.

- Insertion time covers calls to `Simulation::schedule`.
- Dispatch time covers `Simulation::run` with a checksum-producing no-op handler.
- Throughput is event count divided by the corresponding elapsed time.
- Current RSS uses `VmRSS` and peak RSS uses `VmHWM`, both from `/proc/self/status`.
- RSS insertion delta is RSS after insertion minus RSS before insertion.
- Summary values are the median of the three independent runs.

Equivalent commands:

```bash
cmake --preset release -B ~/.cache/nexuslab-build/release
cmake --build ~/.cache/nexuslab-build/release
~/.cache/nexuslab-build/release/simulator/nexuslab_benchmarks --events 1000000
~/.cache/nexuslab-build/release/simulator/nexuslab_benchmarks --events 10000000
```

## Median results

| Events | Insertion time | Insertion throughput | Dispatch time | Dispatch throughput | RSS insertion delta | Peak RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 1,000,000 | 153,339,105 ns | 6,521,493.65 events/s | 191,681,312 ns | 5,216,992.67 events/s | 97,376 KiB | 100,736 KiB |
| 10,000,000 | 1,989,586,378 ns | 5,026,170.32 events/s | 2,167,812,499 ns | 4,612,945.08 events/s | 954,180 KiB | 1,277,788 KiB |

`sizeof(Event)` was 56 bytes and `sizeof(EventPayload)` was 16 bytes. The steady insertion RSS delta
was approximately 99.71 bytes per event at one million events and 97.71 bytes per event at ten
million events. The ten-million-event peak includes temporary allocation during container growth.

## Raw runs

### One million events

| Run | Insertion ns | Insertion events/s | Dispatch ns | Dispatch events/s | RSS before KiB | RSS after insertion KiB | Peak RSS KiB | Checksum |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 153,339,105 | 6,521,493.65 | 181,985,636 | 5,494,939.17 | 3,320 | 100,696 | 100,696 | 7,106,713,294,135,827,890 |
| 2 | 159,053,899 | 6,287,176.90 | 205,764,903 | 4,859,915.30 | 3,360 | 100,736 | 100,736 | 7,106,713,294,135,827,890 |
| 3 | 134,917,315 | 7,411,947.09 | 191,681,312 | 5,216,992.67 | 3,392 | 100,768 | 100,768 | 7,106,713,294,135,827,890 |

Every run dispatched 1,000,000 events, completed with zero pending events, and ended at simulated
time 999,999 ns.

### Ten million events

| Run | Insertion ns | Insertion events/s | Dispatch ns | Dispatch events/s | RSS before KiB | RSS after insertion KiB | Peak RSS KiB | Checksum |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1,989,586,378 | 5,026,170.32 | 2,167,812,499 | 4,612,945.08 | 3,396 | 957,576 | 1,277,788 | 2,852,047,915,862,303,180 |
| 2 | 1,640,048,187 | 6,097,381.82 | 2,225,059,046 | 4,494,262.76 | 3,396 | 957,576 | 1,277,828 | 2,852,047,915,862,303,180 |
| 3 | 2,147,579,415 | 4,656,405.22 | 2,133,707,722 | 4,686,677.51 | 3,396 | 957,576 | 1,277,644 | 2,852,047,915,862,303,180 |

Every run dispatched 10,000,000 events, completed with zero pending events, and ended at simulated
time 9,999,999 ns.

## Baseline interpretation

- The deterministic result checks were stable across every run at each scale.
- Ten million events completed without errors on the accepted WSL2 platform.
- Throughput decreases at the larger scale, so future comparisons must preserve event count.
- Steady resident memory scales close to linearly with event count.
- Peak memory at ten million events is materially higher than steady insertion RSS because container
  growth temporarily retains old and new allocations.

This baseline supplies evidence for Architecture Gate 1. Regression thresholds will be considered
only after this baseline has been reviewed; they are not retroactively inferred from a single
workstation.
