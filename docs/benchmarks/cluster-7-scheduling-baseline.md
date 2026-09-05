<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 7 scheduling and placement baseline

Date: 2026-09-05. Base `2b29ab6ff894f1781f2d3fec4b7d9cd44c852e4e` plus pending Cluster 7 changes.
Source manifest SHA-256: `bb938cb04c07f431384f889282a3f1d72761737940b028628074043209c05c8a`. The manifest contains sorted relative paths and
SHA-256 contents of simulator C++ files, CMakeLists files, cmake modules, scripts, presets and
clang configurations. Each line is `HASH  PATH` followed by newline. Documentation is excluded.
No Git commit was created during this validation.

## Method and repeatability

Run `bash scripts/benchmark-scheduling-suite.sh`: four policies × five workload cases at 128 GPUs,
plus four policies × four inventory sizes for placement planning. Three fresh processes per case
produce 108 runs. Six separate kernel controls (three each at one and ten million events) also ran.
Every process exited successfully. Every non-wall-time/non-RSS field matched across repetitions,
including placement/route/job digests, failures, waits, bytes and final timestamps. Local raw logs
remain outside the repository; tables contain curated medians. No build ran during measurement.

Host: Ubuntu 24.04 ARM64 Docker, Linux 6.10.14-linuxkit, eight Apple ARM virtual CPUs, 7,837 MiB
memory. GCC 13.3 Release, CMake 3.28.3, Ninja 1.11.1; Clang/tidy/format 18.1.3 validate correctness.
These local numbers are not hardware calibration or a comparison against the historical AMD/WSL host.

Workload timing includes graph/runtime construction, admission, simulation and report collection.
Planning timing includes 1,000 calls and explicit output hashing; inventory and policy construction
are excluded. Each planning request asks for 32 GPUs, with job IDs 0–999, on an entirely free
inventory. Peak RSS is whole-process high water, including graph, containers and temporary allocations.
Planning at 8,192 GPUs does not demonstrate full training simulation at that size.

All workload policies receive identical seed 42, ECMP, 128-GPU Clos (8 GPUs/NIC, 8 NICs/rack,
8 spines), 100-Gbit/s fabric, 500-ns propagation, 262,144-byte waiting buffers, 800-Gbit/s independent
local timing, zero local latency, 65,536-byte gradients/buckets, 4,096-byte chunks, 10,000-ns compute,
and two steps. Kernel tracing is disabled; placement/job/collective records are retained.

- Sequential: 16 jobs × 16 workers, arrivals every 20,000 ns from zero. Jobs can still overlap.
- Burst: the same jobs all arrive at 1 ns.
- Mixed: 16 jobs request 4,8,…,32 workers twice, all arriving at 1 ns.
- Failure: burst plus GPU 0 down at 5,000 ns and up at 20,000 ns; the affected owner aborts.
- Rack pressure: a pinned holder occupies GPUs 0–59 at time zero, with 100,000-ns compute,
  one step and 64-byte gradient/bucket. Sixteen 8-worker jobs arrive at 1 ns. Holder uses default
  4,096-byte chunks. All policies see the same pinned rank order.

Dynamic jobs use priority `job_index % 3`; placement and queue order then determine paths and
subsequent admissions. Lower scheduling wait need not imply lower makespan or less GPU idle time.

## End-to-end policy comparison

Wait is summed job scheduling wait, not GPU-nanoseconds. Final time includes draining all traffic.
Rack pressure has 17 jobs; other cases have 16. Failure cases finish 15 successfully and abort one;
all other cases finish every job successfully. Unexpected failures abort the benchmark process.

| Case | Policy | Wall (ms) | Final time (ns) | Summed wait (ns) | Fabric bytes issued | Peak RSS (KiB) |
|---|---|---:|---:|---:|---:|---:|
| sequential | first-fit | 8.253250 | 419,360 | 0 | 7,864,320 | 4,832 |
| sequential | random | 56.298709 | 684,784 | 912,952 | 60,456,960 | 14,940 |
| sequential | rack-local | 8.065166 | 419,360 | 0 | 7,864,320 | 4,832 |
| sequential | compact | 7.927458 | 419,360 | 0 | 7,864,320 | 4,832 |
| burst | first-fit | 8.167042 | 238,721 | 954,880 | 7,864,320 | 4,900 |
| burst | random | 57.263000 | 574,097 | 2,243,536 | 60,702,720 | 15,104 |
| burst | rack-local | 8.499958 | 238,721 | 954,880 | 7,864,320 | 4,900 |
| burst | compact | 8.843125 | 238,721 | 954,880 | 7,864,320 | 4,900 |
| mixed | first-fit | 19.121625 | 641,413 | 1,095,314 | 11,913,268 | 7,416 |
| mixed | random | 102.281708 | 1,090,148 | 2,358,745 | 67,110,258 | 23,708 |
| mixed | rack-local | 17.276708 | 475,765 | 1,003,521 | 11,156,880 | 6,432 |
| mixed | compact | 17.454750 | 621,399 | 908,186 | 9,757,616 | 6,228 |
| failure | first-fit | 7.711625 | 238,721 | 855,519 | 7,372,800 | 4,900 |
| failure | random | 51.990458 | 562,417 | 1,983,175 | 56,033,280 | 14,192 |
| failure | rack-local | 7.395541 | 238,721 | 855,519 | 7,372,800 | 4,892 |
| failure | compact | 7.614958 | 238,721 | 855,519 | 7,372,800 | 4,900 |
| rack-pressure | first-fit | 9.065708 | 248,945 | 689,280 | 7,341,038 | 5,036 |
| rack-pressure | random | 20.054750 | 339,425 | 1,337,857 | 25,920,494 | 7,872 |
| rack-pressure | rack-local | 4.675000 | 218,236 | 178,368 | 1,006 | 4,192 |
| rack-pressure | compact | 4.735375 | 218,236 | 178,368 | 1,006 | 4,192 |

Rack-local and compact reduce summed wait under rack pressure by 74.1%, from 689,280 to
178,368 ns, and makespan from 248,945 to 218,236 ns. Their 1,006 fabric bytes come from the
pinned holder; dynamically placed 8-GPU jobs stay within single NICs. First fit sends 7,341,038
fabric bytes in this case, with four cross-rack ring edges summed over placements. Random sends
25,920,494 fabric bytes. Total delivered logical bytes remain 29,367,680 across all four policies.

On the mixed case, rack-local has the shortest makespan among these policies (475,765 ns), while
compact has the lowest total wait (908,186 ns) but a 621,399-ns makespan. First fit has 10 summed
cross-rack ring edges, rack-local 4, compact 6, and random 130. The greedy compact policy is not
universally superior. On uniform sequential/burst input, first-fit/rack-local/compact have identical
time and byte metrics; their host timing differences do not indicate a simulated-policy advantage.

Local traffic uses independent timing with no shared-bus contention. That assumption favors NIC-local
placement and prevents extrapolating these gains to real hardware. Rank order and placement both
influence communication. This is a controlled synthetic comparison, not an optimal-placement proof.

## Placement planning scale

Median mean time per call, derived from each process's 1,000-call total. Requests always fit;
queue scans, allocation mutation and decision-record costs are measured by workload runs above.

| Inventory GPUs | First-fit (µs/call) | Random (µs/call) | Rack-local (µs/call) | Compact (µs/call) |
|---|---:|---:|---:|---:|
| 64 | 0.662209 | 3.654916 | 0.832666 | 2.137750 |
| 512 | 3.066000 | 39.274500 | 4.525416 | 38.590000 |
| 2,048 | 12.456958 | 192.831126 | 21.149625 | 314.631042 |
| 8,192 | 64.917875 | 941.220209 | 115.381625 | 2012.910960 |

At 8,192 GPUs, median peak RSS ranges from 6,664 to 6,792 KiB. Compact's sorting repeatedly
consults rack/NIC capacity maps, so its ~2-ms planning cost is higher than first fit's ~65 µs.
Resource views borrow the inventory; policy scratch and output are O(G) and O(request size).
There is no all-pairs graph cost matrix. Inventory and retained records have explicit hard limits.

## Kernel controls and regression thresholds

One-million-event median insertion/dispatch: 99,009,417 / 110,152,625 ns, peak RSS 117,128 KiB.
Ten-million-event medians: 1,425,267,251 / 1,996,066,251 ns, peak RSS 1,539,308 KiB.
Event size remains 72 bytes, payload 32 bytes; ten-million final time is 9,999,999 ns and checksum
`2852047915862303180` across repetitions. Wall time varies relative to Milestone 4 measurements.
The previous and current Release kernel executables are byte-for-byte identical, both SHA-256
`2146a56b48c08e557a607c299907348fe47645b33e00196d633ec87df28fdb6c`.
Thus these kernel timings do not establish a scheduler-induced kernel regression or improvement.
The kernel benchmark does not execute scheduling; scheduler overhead is measured separately above.

For the same idle host, investigate repeated median mixed/random wall time above 150 ms or RSS
above 32,000 KiB; investigate other mixed-policy wall times above 30 ms. At 8,192 GPUs investigate
compact planning above 3 ms/call or whole-process RSS above 9,000 KiB. These are local investigation
thresholds, not portable CI timing assertions. Deterministic outcomes and byte conservation remain
mandatory. CI runs the smoke benchmark; hosted CI is pending the user's commit/push.
