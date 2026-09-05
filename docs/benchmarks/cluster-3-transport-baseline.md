<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 3 transport baseline

Date: 2026-09-05. Source: base `8bbb844e14aabaca52d3d927bc83202f7bf65f86` plus the
Cluster 3 completion changes identified by [source hashes](cluster-3-source-sha256.txt).
These changes were measured before the user creates their commit.

## Environment and method

Ubuntu 24.04 container, Linux 6.10.14-linuxkit, aarch64, eight Apple ARM virtual CPUs,
7,837 MiB available system memory, GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Release build.
Tests also used Clang/clang-tidy/clang-format 18.1.3. Docker ran locally on macOS.
No other build was running during measurements. This is a new ARM64 baseline, not a comparison
against the older AMD/WSL2 Cluster 1 and 2 reference machine.

Each case runs in three fresh processes, seed 42, tracing disabled. Runtime begins immediately
before `Simulation::run` and includes transfer submission, chunk partitioning, registration, and
event processing. Topology construction, reporting, and destruction are outside the timed region.
Peak RSS is process high-water memory from `/proc/self/status`, including topology and retained
transport state. Configured buffer bytes constrain simulated queue occupancy; they are not
preallocated payload memory. Chunks represent byte counts, not payload arrays.

Reproduce with `bash scripts/benchmark-transport-suite.sh`. For the kernel controls, run
`bash scripts/benchmark.sh 1000000` and `bash scripts/benchmark.sh 10000000` three times each.
The [raw output](cluster-3-linux-arm64-raw.txt) contains all 36 transport runs and six kernel runs;
the six buffered controls were collected after the original matrix and are appended.
The 100-flow 4-KiB incast is deliberately repeated in both the flow and chunk-size matrices,
so its displayed median uses six samples; other rows use three.

All fabric links use explicit synthetic 100-Gbit/s bandwidth and 500-ns propagation. Clos profiles
use eight GPUs per NIC, eight NICs per rack, and eight uplinks. Routes are caller-supplied through
spine zero when crossing racks; this is a traffic harness, not an ECMP or routing-policy result.
All flows start at simulated time zero. Default transfers are 65,536 bytes, with 4,096-byte chunks
and 262,144-byte waiting buffers. Incast targets NIC zero. All-to-all cycles deterministic distinct
NIC pairs; 4,032 flows cover all ordered pairs of the 64 NICs in the 512-GPU profile exactly once.

## Median measurements

| Pattern | GPUs | Flows | Chunk bytes | Buffer bytes | Events | Runtime ms | Peak RSS KiB | Successful flows |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Pipeline | 2 | 1 | 1 | 333,333 | 1,000,000 | 476.677 | 113,968 | 1 |
| Incast | 512 | 100 | 4,096 | 262,144 | 6,827 | 3.268 | 4,108 | 4 |
| Incast | 512 | 10,000 | 4,096 | 262,144 | 170,907 | 217.472 | 59,924 | 7 |
| All-to-all | 512 | 100 | 4,096 | 262,144 | 9,025 | 3.454 | 4,108 | 100 |
| All-to-all | 512 | 10,000 | 4,096 | 262,144 | 180,193 | 221.378 | 59,872 | 232 |
| All-to-all, complete pair set | 512 | 4,032 | 4,096 | 262,144 | 84,705 | 94.422 | 25,848 | 232 |
| Incast | 512 | 100 | 1,024 | 262,144 | 27,293 | 13.827 | 5,716 | 3 |
| Incast | 512 | 100 | 16,384 | 262,144 | 1,709 | 0.893 | 3,724 | 3 |
| Incast, stretch topology | 2,048 | 100 | 4,096 | 262,144 | 7,435 | 3.643 | 5,516 | 2 |
| Incast, buffered control | 512 | 10,000 | 4,096 | 1,073,741,824 | 1,368,769 | 563.983 | 65,172 | 10,000 |
| All-to-all, buffered control | 512 | 10,000 | 4,096 | 1,073,741,824 | 1,368,321 | 589.149 | 64,352 | 10,000 |

The pipeline contains 333,333 one-byte chunks. Its exact event count is one bootstrap plus three
transitions per chunk (initial arrival, serialization completion, destination arrival). This
satisfies the million-event transport exercise, rather than merely a million no-op kernel events.

Every run verifies that all transfers have exactly one terminal outcome and that delivered plus
dropped bytes equal submitted bytes. Every repeated case produced identical domain output:
event count, final time, outcomes, byte totals, maximum waiting occupancy, and aggregate busy time.
Only wall time and RSS vary. Finite-buffer stress intentionally drops traffic: at 10,000 flows,
incast delivers 1,073,152 bytes and drops 654,286,848; all-to-all delivers 15,859,712 and drops
639,500,288. Both sum to 655,360,000 submitted bytes. There is no automatic retry.

The buffered controls each deliver all 655,360,000 bytes with zero drops and 10,000 successes.
Peak simulated waiting occupancy is 499,056,640 bytes for incast and 62,849,024 for all-to-all.
These controls ensure the scale result is not solely fast rejection of overflowing queues.

Chunk-size sensitivity is visible in both cost and outcomes: 1-KiB incast processes 27,293 events,
while 16-KiB processes 1,709. Delivered bytes change from 931,840 to 950,272 under the same finite
buffer. Therefore chunk size must remain an explicit experiment input; these results do not
establish packet-level fidelity or a hardware-calibrated default.

## Kernel controls and local guardrails

| No-op events | Median insertion ms | Median dispatch ms | Peak RSS KiB |
|---:|---:|---:|---:|
| 1,000,000 | 51.393 | 104.303 | 117,124 |
| 10,000,000 | 619.948 | 1,329.915 | 1,541,456 |

The event envelope remains 72 bytes (32-byte payload), below the 80-byte transport test ceiling.
Kernel checksums and final timestamps repeat exactly. No cross-machine speedup is claimed.
The earlier WSL2 guardrails were not revalidated on their original host.

For future median-of-three runs on this same ARM64 environment, investigate a pipeline runtime
above 650 ms or RSS above 150,000 KiB; investigate either buffered 10,000-flow runtime above
800 ms or RSS above 85,000 KiB. These ceilings allow approximately 30% or more variance.
They are local engineering alerts, not hosted-CI timing assertions. Preserve the exact outcome,
byte-conservation, and event-count results unless an approved semantic change explains differences.

Retained-state limits default to 1,000,000 chunks, 16,000,000 route entries, and 1,024 hops per
route. They bound admitted model state, not a guaranteed process-memory ceiling. Completed chunks
remain inspectable until runtime destruction. Large experiments must be explicitly sized within
these limits; allocation and event-ID exhaustion remain fatal, non-resumable run errors.
