<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Milestone 4 training and Ring AllReduce baseline

Date: 2026-09-05. Base: `ad75ced762feb708f2bd01f7ebb7e6747c7ab316` plus pending Milestone 4 changes.
Source manifest SHA-256: `9bce2323d8589d52b57cf743407ba2e51946cc778dd609bcbcbfc7864816502b`. Sorted relative paths and SHA-256 contents of
simulator C++ files, CMakeLists files, cmake modules, scripts, presets, and clang configurations;
each manifest line is `HASH  PATH` followed by newline. Documentation is excluded.

## Method

Ubuntu 24.04 ARM64 Docker, Linux 6.10.14-linuxkit, eight Apple ARM virtual CPUs, 7,837 MiB
memory; GCC 13.3 Release, CMake 3.28.3, Ninja 1.11.1. Clang/tidy/format 18.1.3 were used
for validation. These are synthetic simulator measurements, not calibrated GPU performance.

`bash scripts/benchmark-training-suite.sh` runs 18 cases in three fresh processes each.
The initial 17-case matrix was measured first; the shared-NIC case was appended after adding
its option and regression test. The final parser refactor/output label did not change simulation
behavior. All 54 processes returned successfully and domain fields matched across repetitions.
Six separate kernel controls and six matched prior/current controls also completed. Raw logs
remain outside the repository. No build ran concurrently with a measured process.

Defaults: seed 42, ECMP, 100-Gbit/s links, 500-ns propagation, 262,144-byte waiting buffers,
65,536-byte gradients/buckets, 4,096-byte chunks, 10,000-ns compute, three steps, no overlap.
Each worker normally uses a different NIC, with GPU `(job * workers + worker) * 8`.
Shared-NIC mode uses `worker * 8 + job`, retaining disjoint GPUs. Clos size rounds up to
64 GPUs. Training wall time includes topology/runtime construction, execution, and report
collection. RSS is whole-process high water, not retained model payload. Timelines are retained;
low-level kernel tracing is disabled. Every successful collective conserves its algorithm bytes.

## Worker scaling

One job, one step; all jobs succeeded. Values are medians of three processes.

| Workers | Wall time (ms) | Simulated finish (ns) | Events | Delivered bytes | Peak RSS (KiB) |
|---|---:|---:|---:|---:|---:|
| 1 | 0.047958 | 10,000 | 2 | 0 | 3,296 |
| 2 | 0.100458 | 17,904 | 163 | 131,072 | 3,280 |
| 4 | 0.216041 | 25,840 | 485 | 393,216 | 3,296 |
| 8 | 0.480625 | 37,776 | 1,129 | 917,504 | 3,296 |
| 32 | 10.170375 | 174,672 | 10,945 | 4,063,232 | 4,784 |
| 128 | 163.931917 | 559,656 | 178,945 | 16,646,144 | 28,496 |

The one-worker path has no communication. Larger rings have more rounds, routes, and events;
128-worker results cross racks and are not a pure linear planner comparison.

## Planner scaling

Each process constructs 1,000 individual round plans; it does not materialize a whole collective.

| Workers | Total planning time (ns) | Mean per round (ns) | Peak RSS (KiB) |
|---|---:|---:|---:|
| 2 | 14,583 | 14.583 | 3,040 |
| 8 | 21,208 | 21.208 | 3,040 |
| 64 | 112,709 | 112.709 | 3,028 |
| 512 | 937,084 | 937.084 | 3,040 |
| 8,192 | 15,118,666 | 15,118.666 | 3,296 |

The 8,192-worker round stores 196,608 bytes of transfer payload (24 bytes per participant),
excluding vector/container overhead. Full network execution was measured through 128 workers;
8,192-worker planning is not evidence of practical full-simulation scale.

## Overlap, contention, and chunk controls

Eight workers and three steps unless specified. Wall times are medians.

| Case | Wall time (ms) | Simulated finish (ns) | GPU idle (ns, summed) | Events |
|---|---:|---:|---:|---:|
| 100,000-ns compute, 16,384-byte buckets, overlap off | 2.969708 | 523,104 | 1,784,832 | 6,745 |
| Same, overlap on | 3.404042 | 355,776 | 446,208 | 6,817 |
| Eight jobs, distinct NICs | 14.853584 | 113,328 | 5,332,992 | 27,080 |
| Eight jobs, shared NICs | 17.937458 | 243,264 | 13,370,752 | 27,080 |
| Zero waiting buffer | 0.082166 | 11,656 | 13,248 | 57 |
| 1,024-byte chunks | 4.597833 | 102,996 | 583,968 | 13,465 |
| 16,384-byte chunks | 1.414250 | 127,104 | 776,832 | 1,705 |

Overlap lowers simulated elapsed time by approximately 32% and idle time by 75%, delivering
the same 2,752,512 bytes. Eight-job runs both deliver 22,020,096 bytes and complete all jobs;
shared NICs raise maximum waiting bytes to 61,440 and extend makespan by about 115%.
Shared-NIC median RSS is 5,632 KiB versus 6,076 KiB for the larger distinct-NIC topology.
This comparison changes placement/topology size as stated; the unit test separately compares
isolated and concurrent jobs on the same topology.

Zero buffer intentionally fails the job: 65,536 bytes issued, 32,768 delivered, explicit failure,
no later collective rounds. A benchmark process succeeds when expected accounting is valid;
the user-facing CLI returns a nonzero exit status for failed/cancelled jobs. Small chunks cost
more events while large chunks extend serialized transfers; neither is universally optimal.

## Kernel controls and local guardrails

Event size remains 72 bytes and payload 32 bytes. The initial one-million-event median insertion/
dispatch times were 93,297,959 / 117,865,334 ns; ten million measured
1,510,721,751 / 1,928,935,792 ns. Because this exceeded earlier local wall times, three alternating
prior/current ten-million-event pairs were run in the same container. Prior medians were
916,932,167 / 1,407,867,542 ns; current medians were 731,483,667 / 1,350,176,251 ns.
Individual runs varied substantially (including warm-up effects). These measurements do not
establish a speedup, but the matched controls do not show a median regression. Both versions
retain roughly 1,539,400 KiB peak RSS, final time 9,999,999 ns, and checksum
`2852047915862303180` for all ten-million-event runs.

For subsequent comparisons on this same idle host, investigate median 128-worker wall time
above 225 ms or RSS above 38,000 KiB, and 8,192-worker planning above 22,000 ns per round.
Always require identical deterministic domain outputs and byte conservation. These are local
investigation thresholds, not portable CI timing assertions. Hosted CI remains pending a user push.
