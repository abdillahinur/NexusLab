<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 9 catalog-v2 telemetry slice evidence

Date: 2026-11-22. Source: parent f19dbdf plus this change. Scripted telemetry, export and typed
full-trace summary reconstruction are implemented. Controlled policy comparisons, overhead
characterization and Gate 9 evidence remain pending. Clusters 0–8 remain gate-approved: 9/25
(36%) by cluster count, not effort. This slice does not approve Gate 9.

Environment: Docker Desktop/Linux ARM64, Ubuntu 24.04.4, GCC 13.3.0, Clang/clang-format/clang-tidy
18.1.3, CMake 3.28.3 and Ninja 1.11.1. Existing image nexuslab-toolchain:milestone4,
sha256:0352d81cce61147cfab6b6c04b6f2a8bf445874e5f09996605d3c12f3e35e6c2;
volume nexuslab-baseline-20261122, four workers, read-only source mount and unchanged pinned
sources. No dependency, warning policy or suppression was changed. Hosted CI was not run.

## Observational contract

- Scripted composition supplies the observational sink to failure/load owners. Metrics 100–104
  count five lifecycle transitions per restored incident, unique incident/job pairs and logical
  down-dropped bytes, immediate zero detection latency, and configured restoration intervals.
  Attribution/health-refresh facts do not increment the lifecycle counter. Permanent/pending
  incidents have no invented recovery/detection samples.
- SummaryBuilder keeps bounded latest incident/request snapshots in all enabled modes. Full
  records retain complete lifecycle, attribution, pending and negative facts. Effective health
  refreshes after another down cause restores; link snapshots include baseline/effective rates
  in both directions. State cardinality is independently bounded by limits.metric_series;
  existing record/edge/sample/serialized-byte limits remain independent and fail explicitly.
- Routing records add request/capacity provenance only in scripted composition. No-route facts
  join the actual routing flow/event, collective and job; background source IDs remain separate
  from incident IDs and do not fabricate affected training jobs or dropped bytes.
- Scripted JSON/JSONL uses catalog 2 and logical schema 1, with script/outcome checksums,
  outcome version, capacity revision, domain_success and explicit observation/record completeness.
  Queue completion remains separate from failed/waiting/pending job/load outcomes. Legacy runs
  retain catalog 1 and their original canonical bytes. No domain digest layout was extended.
- reconstruct_full_trace replays finalized in-memory full records under explicit catalog
  selection. It validates record ID continuity, timestamps, version/provenance compatibility,
  and rebuilds metrics, job attributions and perturbation snapshots rather than copying cached
  summary values. JSONL parsing/import is a separate future boundary.

## Regression and compatibility evidence

Fourteen new FailureTelemetryTest cases extend the prior 316 checks to 330. They cover:

- Full-trace reconstruction for spine link, port/switch recovery, GPU recovery, degradation,
  successful loads, buffer-loss loads and no-route admissions. Tests erase cached views before
  replay and require byte-identical canonical summaries.
- Exact lifecycle/job/drop/latency metrics; missing recovery for permanent incidents; equal
  domain outcomes and observed summary state across off/summary/sampled/full modes.
- Effective-health refresh when a recovered link remains blocked until another cause restores;
  baseline/effective capacity arrays and routing capacity revisions.
- Stopped runs with unapplied incidents and uninjected pending requests, without fabricated
  detection, transfer IDs or completion times; finalized stopped full traces replay as stopped.
- Explicit state/serialized limits, unsupported catalogs, mismatched provenance, partial-mode
  replay rejection, and record sequence gaps.
- Negative CLI outcomes with a completed full record stream and a verifiable footer digest;
  separate incident/source namespaces, an exact additive failure-record golden, catalog-v1
  replay, and the join from no-route cause to its actual routing event/flow and collective.

Existing legacy serialization goldens remain unchanged and pass: summary FNV-1a64
1964dc82cc5227c8 and JSONL FNV-1a64 a0cadfea88f5ba90. Existing script goldens and domain outcome
digest v1/v2/v3/v4 encodings remain unchanged. Observation additions introduce no events or RNG draws.

## Executed Release exports

For each example below, append telemetry: {mode: full} to a temporary copy and execute
/build/baseline/gcc/release/simulator/nexuslab train --file <copy> with
--telemetry-summary-json and --telemetry-records-jsonl separately. Original source examples remain
unchanged. Negative examples intentionally return 1 for both exports.

| Example | Exit | Catalog | Domain success | Final time ns | Latest states | Records | Summary bytes | JSONL bytes |
|---|---:|---:|---|---:|---:|---:|---:|---:|
| no-route-recovery | 1 | 2 | false | 27,300 | 3 | 1,559 | 27,854 | 362,563 |
| link-degradation | 0 | 2 | true | 16,200 | 1 | 1,637 | 30,593 | 381,559 |
| background-load-loss | 1 | 2 | false | 12,550 | 4 | 391 | 17,970 | 93,225 |
| two-worker (legacy) | 0 | 1 | Original format | 53,712 | 0 | 4,871 | 15,613 | 1,129,683 |

All four exports have zero samples because the default 1,000,000-ns sample interval exceeds their
final times. Independent Python json parsing verified record/sample footer counts and recomputed
FNV-1a64 over the exact preceding UTF-8 bytes, excluding the footer. Scripted full records mark
records_complete true while preserving domain_success false for failed jobs/loads. Off/summary/
sampled exports mark records_complete false; off also marks domain_observations_complete false.
These are synthetic correctness exports, not controlled routing-policy comparisons.

## Footprint boundary

A GCC C++20 sizeof probe compiled against parent f19dbdf's records header and the current header
(with identical unchanged dependencies) measured:

| Type | Parent bytes | Current bytes |
|---|---:|---:|
| Simulation event | 72 | 72 |
| Event payload | 32 | 32 |
| Telemetry observation | 160 | 240 |
| Telemetry record | 424 | 536 |

The new value-owned observation/correlation fields add 112 bytes (26.4%) per retained record.
The kernel envelope is unchanged. This is a static footprint measurement; it is not a throughput,
RSS, or regression-budget acceptance result. Full-mode overhead and failure/event-growth controls
remain part of the pending Gate 9 work. Limits bound counts independently of this larger record size.

## Validation

Command: NEXUSLAB_BUILD_ROOT=/build CMAKE_BUILD_PARALLEL_LEVEL=4 bash scripts/validate-baseline.sh.
Result: baseline validation passed. All 330/330 checks pass under GCC Debug, GCC Release, Clang
Debug, Clang ASan/UBSan with leak detection, and the clang-tidy build. Formatting and git diff
--check pass. Registered simulation, topology, transport, routing, training, scheduling and
telemetry smoke benchmarks remain part of the matrix. Raw logs, Release exports, exit statuses
and the footprint probe output are retained outside the source checkout in the task workspace.
