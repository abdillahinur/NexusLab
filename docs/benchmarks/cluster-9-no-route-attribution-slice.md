<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 9 no-route job attribution slice evidence

Date: 2026-11-22. Source: parent d3de80b plus this change. This completes no-route affected-job
attribution, not Cluster 9 or Gate 9. Catalog-v2 failure/load observations, JSON/JSONL export,
full reconstruction, locked policy comparisons and final gate evidence remain pending.
Clusters 0–8 remain gate-approved: 9/25 (36%) by cluster count, not effort.

Environment: Docker Desktop/Linux ARM64, Ubuntu 24.04.4, GCC 13.3.0, Clang/clang-format/clang-tidy
18.1.3, CMake 3.28.3, Ninja 1.11.1. Existing image nexuslab-toolchain:milestone4,
sha256:0352d81cce61147cfab6b6c04b6f2a8bf445874e5f09996605d3c12f3e35e6c2;
volume nexuslab-baseline-20261122, four build workers, read-only source mount and unchanged
pinned dependency sources. No warning suppression, policy relaxation or dependency was added.
Hosted CI was not run.

## Ownership and attribution

A failed collective admission has no transfer ID. The ring retains a small admission fact until
the training outcome pump runs, after WorkloadEngine registers the collective/job mapping and
before terminal results remove that mapping. Each failed collective produces at most one fact,
bounded by the existing maximum_collectives limit. Composition drains them even without a failure
runtime. No observer lifetime or callback cycle is introduced.

The coordinator computes structural fabric distances from each endpoint, ignoring current health.
An arc qualifies precisely when its source distance plus one hop plus its destination distance
equals the healthy minimum-hop distance. Matching active link/port/switch incidents use the same
resource-key selection as down-dropped chunks; lowest declaration ID wins. This inspects all
minimum-hop arcs without enumerating paths or inheriting routing candidate caps. GPU health,
degradation, recovered causes and failures only on longer paths do not qualify. A job contributes
at most one pair per incident under the existing affected-job bound. This is the accepted
deterministic attribution rule, not proof that a single incident was the sole cause.

No-route admissions add affected jobs without fabricating transferred/dropped bytes or chunks.
Background no-route requests remain separate workload outcomes and add no fake training jobs.
Per-admission cause provenance in exported records remains catalog-v2 work. Existing domain digest
v1/v2/v3/v4 layouts and script golden vectors are unchanged: newly corrected affected-job pairs
enter the existing hash fields rather than silently extending the encoding.

## Regression coverage

Eight new FailureTest checks extend 308 checks to 316:

- Link, port and switch disconnection before admission each attribute the failed job with zero
  dropped traffic; a new job succeeds after restoration.
- Overlapping incidents select the lowest declaration ID regardless of onset order. Once that
  incident recovers, the remaining cause receives the next independent failed job.
- A spine outside a same-leaf minimum path and an active capacity degradation receive no blame;
  the leaf-down cause receives the job.
- An unscripted disconnection fails the job without inventing an incident.
- Sequential failed jobs with an affected-job pair limit of one produce an explicit failed
  simulation on the second pair; the first pair remains and no drop facts appear.
- A 70-spine topology attributes an incident on the last healthy minimum-hop candidate, beyond
  the router's default 64-candidate limit, without enumerating the healthy path set.
- Off/summary/sampled/full preserve corrected reports and domain digests. A background no-route
  request does not inflate the incident's affected-job count.
- The checked-in CLI recovery demo preserves failed/succeeded jobs, zero dropped bytes/chunks,
  separate load outcomes and the expected exit status 1.

## Executed Release demo

Command: /build/baseline/gcc/release/simulator/nexuslab train --file
examples/training/no-route-recovery.yaml --timeline. Exit status 1 is expected.

| Fact | Observed value |
|---|---|
| Simulation queue outcome | Completed; 139 dispatched events; final time 27,300 ns |
| Job 0 | Failed its first admission at 1,000 ns; zero steps completed |
| Job 1 | Arrives at 6,000 ns; succeeds after three steps at 27,300 ns |
| Switch incident 1 | Applied/detected at 0; restored at 5,000 ns; one affected job |
| Attributed dropped traffic | 0 bytes, 0 chunks, no fabricated transfer |
| Background request 0 | NoRoute at 1,000 ns, no transfer ID or delivered/dropped bytes |
| Background request 1 | Injected at 7,000 ns; delivers 1,000 bytes at 9,050 ns |
| Script identity / outcome layout | 45c76322a314df1c / version 4 |

These are executed synthetic correctness outcomes, not policy comparisons or hardware results.
Restoration enables new work and does not restart the failed job.

## Validation

Command: NEXUSLAB_BUILD_ROOT=/build CMAKE_BUILD_PARALLEL_LEVEL=4 bash scripts/validate-baseline.sh.
Final result: baseline validation passed. All 316/316 checks pass under GCC Debug, GCC Release,
Clang Debug, Clang ASan/UBSan with leak detection, and the clang-tidy build. Formatting and
git diff --check pass. The eight added regression checks preserve the earlier script/digest
goldens and the existing negative job/load outcomes.
Raw validation logs and Release demo output are retained in the task workspace outside the source
checkout. No new event payload, priority, kernel ordering rule or digest field was introduced;
existing simulation/transport/routing/training/scheduling/telemetry benchmark smoke checks remain
part of the full matrix.
