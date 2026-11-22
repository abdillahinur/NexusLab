<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 9 first link-failure slice evidence

Date: 2026-11-22. This records the first implementation slice, not Gate 9 approval or a completed
failure-policy benchmark. Source: parent b5ae01b plus this change. No dependency pins changed.

Environment: Docker Desktop/Linux ARM64, Ubuntu 24.04.4, CMake 3.28.3, Ninja 1.11.1,
GCC 13.3.0 and Clang/clang-format/clang-tidy 18.1.3. Existing local image
nexuslab-toolchain:milestone4, sha256:0352d81cce61147cfab6b6c04b6f2a8bf445874e5f09996605d3c12f3e35e6c2.
Build artifacts use volume nexuslab-baseline-20261122 with four build workers. Reused dependencies
are at the exact pinned revisions; changed project targets are rebuilt. Hosted CI is not claimed.

## Validation

Command: NEXUSLAB_BUILD_ROOT=/build CMAKE_BUILD_PARALLEL_LEVEL=4 bash scripts/validate-baseline.sh.
Final result: baseline validation passed. All 286/286 CTest checks passed independently under
GCC Debug, GCC Release, Clang Debug, Clang ASan/UBSan with leak detection, and the clang-tidy build.
Formatting and git diff --check passed. No new suppression or warning-policy change was introduced.
The final test helpers use checked optional access and the repository's existing assertion-helper
style so macro expansion does not inflate per-test cognitive complexity.

Ten new tests cover exact-time completion cancellation and queued drops; post-restoration admission;
propagation across a permanent failure; ordering/overlap validation; stopped/pending actions;
a 512-GPU leaf–spine fixture; repeat/mode invariance and shared script identity across policies;
an independently encoded script digest; explicit attribution exhaustion; strict parsing; and the
checked-in CLI demo, including refusal of incomplete JSON export. Existing golden fixtures protect
legacy no-script results. The script digest golden 8179943a53468c39 was calculated independently
using Python struct.pack('<Q', value), the canonical two-GPU-direct topology, and FNV-1a.

## Executed Release demo

```bash
/build/baseline/gcc/release/simulator/nexuslab train \
  --file examples/training/spine-link-failure.yaml --timeline
```

Expected and observed exit: 1 (first job failed). Script digest: 0b50e5d295ccd89c; domain digest
version: 2. Actual output has 171 dispatched events, final simulated time 51,200 ns, ten routing
decisions and maximum waiting occupancy 1,000 bytes. Link 576's incident drops 4,000 bytes in four
chunks and affects one job. It is applied/detected at 2,500 ns and restored at 30,000 ns.

| Job | Arrival ns | Outcome | Finish ns |
|---|---:|---|---:|
| interrupted | 0 | Failed | 4,050 |
| during-outage | 5,000 | Succeeded through surviving path | 16,200 |
| after-restoration | 40,000 | Succeeded; restored path eligible | 51,200 |

The topology fixture verifies that the chosen link joins a leaf and spine. Tests inspect actual
selected paths, exclude the down link for outage admissions and require reuse after restoration
under shortest-path routing. ECMP/queue-aware checks hold script identity fixed; no policy winner
or recovery-algorithm comparison is claimed. All four telemetry modes produce equal incident
reports and domain digests. Failed work is never retried or relabeled successful.

## Limits and remaining scope

The coordinator reuses existing link-state events; no event variant or kernel queue change occurs.
The executed Release control was /build/baseline/gcc/release/simulator/nexuslab_benchmarks
--events 1000000. It dispatched exactly 1,000,000 events at final simulated time 999,999 ns with
checksum 7106713294135827890. Event size was 72 bytes and payload size 32 bytes. Insertion took
142,753,125 ns (7,005,100.59 events/s); dispatch took 144,666,000 ns (6,912,474.25 events/s).
Peak RSS was 117,164 KiB and insertion RSS delta 114,248 KiB. This single local control ran during
validation; it is not a performance regression study or a comparison to WSL2 gate thresholds.

Port/switch/GPU incidents, capacity changes, background traffic, no-route incident/job attribution,
catalog-v2 emission/export/full reconstruction, broader failure overhead/scale measurements and
Gate 9 remain pending. This slice exposes complete incident evidence in domain snapshots/human
output, and blocks scripted CLI JSON export rather than implying the old catalog is complete.
