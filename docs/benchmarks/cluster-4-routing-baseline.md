<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 4 routing baseline and policy comparison

Date: 2026-09-05. Source: base `f419a28d7e578a0d6bef3af6553d24baf5e9013b` plus the pending Cluster 4 changes.
Source manifest SHA-256: `a0888b1672b534ca535a5e48651b990690596e8c3e0b8f27958d358907b9f13e`. The manifest is sorted relative paths and
SHA-256 contents of simulator C++ files, CMakeLists files, cmake modules, scripts, presets, and
clang-format/clang-tidy configurations, each line formatted `HASH  PATH` with a final newline.
Documentation does not participate in this fingerprint. No commit was created by the agent.

## Reproduction and measurement boundaries

Run `bash scripts/benchmark-routing-suite.sh` on Linux: 32 cases, each in three fresh processes
(96 runs). The committed report contains curated measurements; local raw execution logs are not
staged. All 96 processes exited successfully. Every field other than wall-clock timing and RSS
matched exactly across repetitions, including decision and outcome digests for traffic runs.

Host: Ubuntu 24.04 ARM64 Docker container, Linux 6.10.14-linuxkit, eight Apple ARM virtual CPUs,
7,837 MiB system memory, GCC 13.3.0 Release, CMake 3.28.3 and Ninja 1.11.1. No build ran during
measurement. Clang, clang-tidy and clang-format are 18.1.3. These local numbers are not a
comparison against the historical AMD/WSL2 baseline or real-fabric performance.

All cases use seed 42, tracing disabled, synthetic 100-Gbit/s fabric arcs, 500-ns propagation,
262,144-byte waiting buffers, 65,536-byte transfers, and 4,096-byte chunks. Clos profiles use
eight GPUs/NIC, eight NICs/rack, and eight spines. Traffic starts one new transfer every 100 ns;
all policies receive the same endpoints, bytes, timing and seed. Transfer routes stay pinned.
The borrowed current-queue view makes no reservation for traffic that has yet to arrive at a hop.

Incast cycles every nonzero NIC as source and targets NIC zero. The all-to-all pattern starts at
`source = flow % NICs`, with `destination = (source + NICs/2 + (flow/NICs) % (NICs/2)) % NICs`.
Divisions are integer divisions. This is a deterministic cross-rack-biased pair sequence; it is not a claim to enumerate every
ordered pair in each run. Its purpose is a controlled, identical routing comparison.

Traffic runtime includes the simulation loop, path lookup, policy selection, transport submission,
chunk processing, and decision retention. Topology/runtime construction and final reporting are
excluded. Transfer latency is completion timestamp minus submission timestamp. p50 uses the lower
median; p95 uses nearest rank. Both percentiles include only fully successful transfers, so always
read them with success counts and loss; fast failed flows do not enter these latency statistics.

Lookup mode measures one cold pair, 10,000 repeated warm queries of that pair, and 10,000 policy
choices on its idle candidate set with varying flow keys. Selection includes the policy's result
construction and current per-hop reads, but excludes path lookup. Afterwards it fills the lazy
cache from NIC zero to every other NIC to measure retained route payload and process peak RSS.
No all-pairs table is precomputed. RSS includes topology, transport, cache and temporary allocations;
route-payload bytes exclude vector/map overhead and are not claimed as total cache memory.

## Path lookup and selection medians

Times below are per query/choice (total loop time divided by 10,000 for warm lookup and selection).
At 64 GPUs there is one two-hop path; larger profiles use eight four-hop paths for the measured pair.
Cold lookup does not depend on policy, so differences between its rows represent run variance.

| Policy | GPUs | Cold lookup µs | Warm lookup ns | Selection ns | Retained route payload bytes | Process peak RSS KiB |
|---|---:|---:|---:|---:|---:|---:|
| shortest-path | 64 | 2.958 | 11.8 | 20.9 | 224 | 3188 |
| shortest-path | 512 | 10.000 | 10.9 | 19.7 | 28896 | 3580 |
| shortest-path | 2048 | 44.084 | 12.0 | 16.7 | 127200 | 5116 |
| shortest-path | 8192 | 182.500 | 10.7 | 18.0 | 520416 | 11040 |
| ecmp | 64 | 2.917 | 9.9 | 39.2 | 224 | 3196 |
| ecmp | 512 | 9.708 | 9.7 | 39.1 | 28896 | 3576 |
| ecmp | 2048 | 31.625 | 11.4 | 40.1 | 127200 | 5116 |
| ecmp | 8192 | 252.458 | 10.3 | 40.0 | 520416 | 11040 |
| least-loaded | 64 | 3.125 | 12.0 | 38.6 | 224 | 3196 |
| least-loaded | 512 | 13.792 | 9.9 | 512.1 | 28896 | 3580 |
| least-loaded | 2048 | 35.959 | 10.0 | 668.9 | 127200 | 5116 |
| least-loaded | 8192 | 168.375 | 9.8 | 681.6 | 520416 | 11040 |
| queue-aware | 64 | 3.166 | 9.7 | 186.3 | 224 | 3196 |
| queue-aware | 512 | 10.333 | 9.7 | 3183.1 | 28896 | 3580 |
| queue-aware | 2048 | 33.042 | 9.8 | 3316.9 | 127200 | 5116 |
| queue-aware | 8192 | 169.167 | 9.9 | 3347.6 | 520416 | 11024 |

The 2,048-GPU sample has 255 cached pairs; the 8,192-GPU sample has 1,023, below the default
1,024-pair cap. The view object itself is eight bytes on this host. Cache hits avoid BFS and path
allocation; policy scores still read live queues on every selection. On 1,000-flow incast, 937 of
1,000 lookups hit (93.7%). The all-to-all sequence has zero hits in these cases: its working set
exceeds the reuse window, showing that the cache is not universally beneficial. Bounds prevent
unbounded growth, while miss cost remains visible in total runtime.

## Identical-traffic comparison

| Policy | Pattern | GPUs | Flows | Succeeded | Dropped bytes | Success p50 ns | Success p95 ns | Runtime ms | Peak RSS KiB |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| shortest-path | incast | 512 | 1000 | 23 | 63418368 | 27560 | 71148 | 35.444 | 7548 |
| shortest-path | all-to-all | 512 | 1000 | 125 | 53514240 | 29996 | 30824 | 45.703 | 8572 |
| shortest-path | all-to-all | 2048 | 1000 | 125 | 25075712 | 29548 | 29864 | 85.323 | 9852 |
| shortest-path | all-to-all | 512 | 10000 | 2202 | 491069440 | 29548 | 34800 | 489.750 | 44488 |
| ecmp | incast | 512 | 1000 | 15 | 63488000 | 64744 | 71164 | 44.779 | 7676 |
| ecmp | all-to-all | 512 | 1000 | 1000 | 0 | 8232 | 8232 | 67.850 | 8700 |
| ecmp | all-to-all | 2048 | 1000 | 1000 | 0 | 8232 | 8232 | 86.164 | 9852 |
| ecmp | all-to-all | 512 | 10000 | 10000 | 0 | 8232 | 8560 | 719.225 | 44832 |
| least-loaded | incast | 512 | 1000 | 14 | 63438848 | 27556 | 50164 | 43.879 | 7676 |
| least-loaded | all-to-all | 512 | 1000 | 169 | 21778432 | 30208 | 34600 | 67.975 | 8700 |
| least-loaded | all-to-all | 2048 | 1000 | 128 | 23363584 | 29880 | 29880 | 79.995 | 9980 |
| least-loaded | all-to-all | 512 | 10000 | 3767 | 153944064 | 27584 | 37424 | 686.857 | 44676 |
| queue-aware | incast | 512 | 1000 | 14 | 63438848 | 27556 | 50164 | 44.287 | 7676 |
| queue-aware | all-to-all | 512 | 1000 | 169 | 21778432 | 30208 | 34600 | 67.863 | 8700 |
| queue-aware | all-to-all | 2048 | 1000 | 128 | 23363584 | 29880 | 29880 | 85.437 | 9980 |
| queue-aware | all-to-all | 512 | 10000 | 3767 | 153944064 | 27584 | 37424 | 764.027 | 44676 |

Every process verifies exactly one outcome per submitted transfer and delivered plus dropped bytes
equal submitted bytes. Decision digests encode flow key, time, revision, candidate count, score,
and every directed hop with explicit integer byte order. Outcome digests encode terminal order,
transfer identity, status, timestamp, delivered counts and each drop reason's counts.

ECMP completes all 10,000 all-to-all flows without loss in this workload. Canonical shortest-path
concentrates cross-rack traffic on one spine. Least-loaded and queue-aware substantially reduce
bytes dropped versus that static path but complete fewer flows than ECMP. Their choices and
outcomes coincide here because links are homogeneous and chunk sizes are identical; a loaded
heterogeneous-bandwidth unit test verifies that their scoring semantics can select different paths.

Current queue state cannot account for all already-submitted traffic still approaching a hop.
Canonical ties can concentrate admissions before those queues reflect the selected work. This is
an accepted limitation of these instantaneous baselines, not evidence that queue-aware routing is
generally inferior. Reservations, delayed telemetry models, hysteresis, and flowlets need a new
architecture decision and workload evidence. The fixed route of a single transfer never oscillates.

All incast policies face the same destination bottleneck and suffer heavy loss at this offered
load. A lower successful-transfer p95 with fewer successes is not an overall improvement. These
results support reproducible policy comparison, not a universal winner or hardware fidelity claim.

## Local guardrails and scope

For median-of-three release runs on the same ARM64 host, investigate 2,048-GPU cold pair lookup
above 75 µs, warm lookup above 25 ns, queue-aware idle selection above 5,000 ns, or lookup-process
peak RSS above 7,000 KiB. For the 512-GPU 10,000-flow queue-aware case, investigate runtime above
1,050 ms or peak RSS above 60,000 KiB. These ceilings allow substantial workstation variance;
they are local engineering alerts, not hosted-CI timing assertions. Preserve byte conservation,
decision/outcome reproducibility, and path validity on every host.

The path service defaults to 64 complete candidate paths, 64 hops, 1,024 cached pairs, and 262,144
cached directed-hop entries. Oversized candidate sets fail explicitly, without a biased truncated
ECMP set. New admissions after link/port/switch failure route around surviving fabric paths;
existing affected chunks may drop and are not retried. Recovery invalidates cached disconnection.

No kernel event envelope, event queue, or topology adjacency algorithm changed. Existing kernel
and topology tests remain in the full suite, including topology scales through 8,192 GPUs.
