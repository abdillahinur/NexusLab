<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 5

Date: 2026-09-05. Decision: approved. Milestone 4 workload scope is complete.

| Requirement | Evidence | Result |
|---|---|---|
| Job model and explicit worker assignment | Validated GPU IDs, overlap conflicts fail at arrival, bounded retained jobs | Pass |
| Compute → collective → barrier lifecycle | Typed events and explicit job states; later steps wait for all buckets | Pass |
| Single/two-worker analytical completion | One worker: 3,000 ns; two workers: 3,450 ns over three steps | Pass |
| Worker synchronization and stragglers | 1,000/2,000-ns workers finish at 2,150 ns; summed compute 3,000, idle 1,300 | Pass |
| Bucket overlap | Analytical 1,100 vs 1,200 ns; benchmark preserves bytes while reducing idle | Pass |
| Multiple jobs and staggered arrivals | Disjoint jobs, delayed arrivals, GPU conflict and shared-NIC contention tests | Pass |
| Failure/cancellation lifecycle | Before arrival, during compute/communication, worker and fabric failures | Pass |
| Configuration-driven profiles | Seven labeled synthetic profiles; strict bounded versioned YAML | Pass |
| Completion and GPU idle metrics | Full and partial compute accounting, terminal outcomes, linked timelines | Pass |
| Determinism | Repeat scenarios compare jobs, collective outcomes, timelines and route records | Pass |

## Validation

198/198 CTest checks pass with GCC 13.3, Clang 18.1.3 plus clang-tidy, and Clang
AddressSanitizer/UndefinedBehaviorSanitizer. Warnings are errors; no warning suppressions or
dependencies were added. Repository clang-format and `git diff --check` pass. Tests include
training benchmark and scenario CLI smoke checks, also wired into CI. Hosted CI awaits a push.

Commands: `cmake --build DIRECTORY`, then `ctest --test-dir DIRECTORY --output-on-failure`
for `/tmp/build-gcc`, `/tmp/build-lint`, and `/tmp/build-sanitize` in the Linux validation container;
release benchmarks use `/tmp/build-release`. Fresh builds can use the existing dev/lint/sanitize/
release presets. Formatting uses `bash scripts/format.sh --check`.

The [shared benchmark report](../benchmarks/milestone-4-training-baseline.md) contains source
fingerprint, environment, repeatability checks, scale measurements, and local regression thresholds.
The [scenario guide](../training-scenarios.md) defines configuration, CLI, metrics, and limitations.

## Boundary and assumptions

[ADR-008](../adr/ADR-008-training-workload-lifecycle.md) defines the lifecycle interface and bounds.
The job engine submits through `CollectiveExecutor`; it does not choose routes or simulate links.
GPU idle is assigned-worker elapsed time minus compute time, including communication stalls.
Partial failure/cancellation reports only compute actually elapsed. Completion is a job state,
not merely an empty simulation queue. Timelines join job/step/bucket to collective identity.

Compute durations are fixed synthetic inputs. Overlap uses evenly spaced bucket readiness and
has no compute slowdown from communication. Profiles including checkpoint, MoE and inference
are parameter proxies, not storage, expert-routing, or inference implementations. Priority is
metadata; automatic placement/preemption/admission queues belong to Cluster 7. Worker failure
is job-scoped, not persistent hardware state. Issued traffic drains after cancellation, and can
contend with later jobs reusing released GPUs. Exhausted retained-state limits are fatal errors,
not resumable checkpoints. These accepted limits are explicit in the CLI guide and ADR.
