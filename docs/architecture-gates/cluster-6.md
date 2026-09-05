<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 6

Date: 2026-09-05. Decision: approved. Milestone 4 Ring AllReduce scope is complete.

| Requirement | Evidence | Result |
| Replaceable collective interface | Separate request/outcome interface, planner and execution adapter | Pass |
| Reduce-scatter and all-gather | Deterministic participant order, P−1 rounds per phase, round barriers | Pass |
| Participant and reduction coverage | Symbolic shard coverage tests for 2, 4 and 8 workers | Pass |
| Analytical volume and uneven shards | 2(P−1)B bytes, quotient/remainder splits, zero-byte shards, P=1 | Pass |
| Chunk dependencies and completion | Transport chunks within rounds; no next round/step before completion | Pass |
| Two/four-worker ideal timing | Two-worker 150-ns collective; four-worker 600-ns collective | Pass |
| Local and fabric paths | Explicit same-NIC timing; GPU→NIC→router→transport for remote transfers | Pass |
| Explicit failure outcomes | No route, buffer loss, fabric failure, cancellation, current-round drain | Pass |
| Metrics and timeline | Issued/local/fabric/delivered bytes, phase/round, terminal outcome, linked jobs | Pass |
| Performance and bounded memory | One-round O(P) planner through 8,192; execution through 128; congestion matrix | Pass |

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

## Separation and accepted model

[ADR-009](../adr/ADR-009-ring-allreduce-execution.md) records the Ring formulas and alternatives.
The pure planner emits logical peer/shard transfers. The adapter maps participants to NICs and
calls the existing router. Routing policies remain independent; transport owns chunk timing,
queues and link failures. A future Tree planner can implement the collective interface without
changing the workload lifecycle or routing policies.

Global round barriers conservatively enforce dependencies. There are no parallel NCCL channels,
cross-round pipelining, reduction arithmetic costs, or hardware calibration. Participant order is
configuration order, not an automatic topology-aware optimization. Local transfers use explicit
independent bandwidth/latency timing, without shared-bus contention. Failure stops new rounds
and drains issued transfers; exactly one collective outcome is emitted. Successful delivered
bytes must equal planned volume. Cancellation can finish a job before its outstanding traffic
drains; the scenario runner verifies every collective is drained before returning.

Job/collective timelines are structured data suitable for later visualization; the CLI provides
readable timeline output. A GUI, Tree/hierarchical algorithms and persistent hardware failures
are outside this milestone. Gates 5 and 6 together complete Milestone 4.
