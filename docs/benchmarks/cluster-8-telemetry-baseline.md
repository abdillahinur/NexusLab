<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 8 telemetry overhead baseline

Date: 2026-09-10. Source commit:
`272afe23f9c616a7bc54fccc7671e389af458861`. Release benchmark executable SHA-256:
`4dac2052fb7800a0b6af080b4ac3b8376c4a7f03099a2bee834cfa7c91d8caa2`.
Pending changes during measurement were documentation and example scenarios only; no benchmark or
simulator source changed. No Git commit was created during this validation.

## Method

The benchmark runs the same deterministic scenario in `off`, `summary`, `sampled`, and `full`
modes. Each value below is the median of five fresh processes, run sequentially in that order with
no discarded warm-up. The elapsed range reports the minimum and maximum of those five runs. The
benchmark's internal steady-clock timer excludes process/WSL startup, YAML parsing, and canonical
serialization. It includes topology/runtime construction, simulation, telemetry collection, and
report construction. Peak RSS is read after JSON and JSON Lines serialization, so it includes
retained telemetry and serialization temporaries. Output sizes are deterministic byte counts, not
filesystem allocation or compressed size.

Host: WSL2 Ubuntu 24.04, Linux 6.18.33.2-microsoft-standard-WSL2, x86-64, AMD Ryzen 9 5900X
(12 cores/24 threads), 15,951 MiB WSL-visible memory. GCC 13.3.0 Release, CMake 3.28.3, Ninja
1.11.1. Clang 18.1.3 supplied the independent Clang/tidy/sanitizer validation, not these timings.

Scenario: synthetic 512-GPU Clos, one 16-worker job, two steps, 10,000-ns compute, 65,536-byte
gradient, 16,384-byte buckets, 4,096-byte chunks, overlap enabled, seed 42, ECMP routing, first-fit
scheduling, and 100,000-ns sampling interval. All default telemetry limits remained resolved and
printed. Every process completed 5,889 simulation events.

```bash
bash scripts/build.sh release
for mode in off summary sampled full; do
  for repeat in 1 2 3 4 5; do
    ~/.cache/nexuslab-build/release/simulator/nexuslab_telemetry_benchmarks --mode "$mode"
  done
done
```

On native Linux, replace the executable prefix with `build/release`.

## Results

| Mode | Median elapsed (ms) | Elapsed range (ms) | Median events/s | Median peak RSS (KiB) | Records | Samples | Summary JSON (bytes) | Records JSONL (bytes) | Total serialized (bytes) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 2.516114 | 2.449237–2.996539 | 2,340,513 | 5,280 | 0 | 0 | 10,797 | 720 | 11,517 |
| summary | 8.107117 | 7.514030–9.598324 | 726,398 | 5,300 | 0 | 0 | 16,122 | 724 | 16,846 |
| sampled | 33.170053 | 31.760095–33.681895 | 177,539 | 5,824 | 481 | 80 | 16,122 | 223,479 | 239,601 |
| full | 80.143519 | 72.699513–83.364701 | 73,480 | 52,660 | 47,999 | 80 | 16,119 | 10,434,790 | 10,450,909 |

Relative to the `off` median, elapsed time is 3.22× in summary, 13.18× in sampled, and 31.85× in
full mode. Median peak RSS is 1.00×, 1.10×, and 9.97× respectively. These ratios describe a short,
record-dense synthetic case on this host; they are not a hardware-performance claim and should not
be generalized to workloads with a different observation/event ratio.

The small size difference between summary- and full-mode summary JSON is expected. The three-byte
difference comes from the `summary` versus `full` mode text included in canonical metadata, not
from domain results or metric values.
`off` still serializes provenance and the complete metric catalog, even though it has no metric
series.

## Determinism and gate interpretation

Every run produced scenario digest `6136446096817804825`, domain outcome digest version 1, and
domain digest `16050562244659516157`. Record, sample, and byte counts were identical within each
mode. The all-mode digest test independently proves domain equivalence, while the full-trace rebuild
test proves that consuming every retained full record reproduces the live canonical summary
byte-for-byte.

This measurement establishes the first Cluster 8 telemetry-overhead baseline. It deliberately does
not define timing or memory regression thresholds from a single measurement session. Thresholds
will be proposed only after comparable repeated baselines show normal same-host variance, following
the baseline-before-threshold policy established in Cluster 1. Deterministic digests, schema golden
files, explicit limit failures, and summary/full reconstruction remain mandatory correctness checks
now; host timings are not CI pass/fail assertions.

Full mode completed the approved 512-GPU target with explicit bounded retention. It is intended for
auditable runs, not as the default scale mode. Summary is the default; sampled provides decision
records and bounded time-series inspection; off is available for controlled overhead studies.
