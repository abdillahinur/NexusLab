<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 8

Date: 2026-09-10. Scope: Telemetry and Observability / first half of Milestone 6 Failures and
Telemetry. Decision: approved. **Proceed: YES.**

| Requirement | Evidence | Result |
|---|---|---|
| Versioned metric registry | Static 45-definition catalog with stable IDs, names, descriptions, kinds, units, label masks, and fixed histogram boundaries; invalid catalogs rejected | Pass |
| Counters, gauges, and histograms | Checked typed operations, stable series order, fixed inclusive buckets plus overflow, counter/aggregate overflow tests | Pass |
| Deterministic time-series samples | Piecewise-constant counter/gauge sampling, equal-time deferral, final flush, empty-period skip, overflow-safe interval progression | Pass |
| Typed event and decision traces | Simulation, job, collective, transfer, queue, routing, placement, failure, and metric record variants with deterministic record IDs | Pass |
| Cross-domain correlation | Optional typed resource/event/cause/job/collective/transfer/chunk/decision/failure IDs; end-to-end chain tests | Pass |
| Summary and slowdown attribution | One incremental builder; terminal validation and explicit scheduling/compute/communication/synchronization/straggler/other job intervals | Pass |
| Four configurable cost levels | `off`, `summary`, `sampled`, and `full`; strict YAML fields and mode-specific retention rules | Pass |
| Canonical serialization | Version-1 deterministic summary JSON and records JSONL, metadata/catalog/header/footer/digest, golden hashes, byte limit, major-version rejection | Pass |
| Full reconstruction | Complete full records rebuild metric series and job attribution; rebuilt canonical summary equals the live summary byte-for-byte | Pass |
| Observational behavior | Identical domain outcome digest in all four modes; telemetry never schedules events, draws RNG, invokes policies, or mutates runtime state | Pass |
| Explicit bounded retention | Separate positive limits for series, decisions, domain records, samples, histogram boundaries, correlation edges, and serialized bytes; exhaustion fails | Pass |
| Terminal and missing behavior | Success, failure, cancellation, and unfinished jobs tested; unfinished attribution is nonterminal and emits no false completion metric | Pass |
| Replay-facing completeness | Provenance, terminal/completeness state, catalog, metrics, attributions, typed records, samples, correlations, and footer digest are UI-consumable | Pass |
| Telemetry overhead known | Five fresh Release processes in each mode at 512 GPUs; wall time, events/s, RSS, retained data, serialized bytes, and domain digest recorded | Pass |

## Correctness and quality

**276/276 CTest checks pass** under GCC 13.3 Debug and Release, Clang 18.1.3 with clang-tidy,
and Clang AddressSanitizer/UndefinedBehaviorSanitizer. Both compilers use warnings as errors.
Formatting and `git diff --check` pass. The suite covers registry validation, record variants,
correlation, session reset and isolation, sampling boundaries, summary attribution, mode invariance,
trace reconstruction, terminal/missing outcomes, schema compatibility, serialization limits, CLI
summary/records smoke tests, and the benchmark smoke test. Hosted CI remains pending this
documentation commit and push.

The [metric catalog](../design/telemetry.md) documents all 45 names, units, kinds, labels,
aggregations, histogram boundaries, and missing-data behavior. The
[benchmark report](../benchmarks/cluster-8-telemetry-baseline.md) records the exact source and binary
fingerprints, host, commands, five-run medians/ranges, deterministic counts, and interpretation.
No new dependency, background thread, warning suppression, or Protobuf surface was added.

## Demo evidence

The checked-in demo uses seven simultaneous, disjoint two-worker jobs from racks 1–7 into distinct
GPUs in rack 0. Deterministic shortest-path routing makes them share the first spine-to-rack path,
so this is a rack-level incast case, not multiple senders targeting one GPU receiver.

```bash
bash scripts/build.sh release

~/.cache/nexuslab-build/release/simulator/nexuslab train \
  --file examples/training/telemetry-incast-summary.yaml \
  --telemetry-summary-json > /tmp/nexuslab-incast-summary.json

~/.cache/nexuslab-build/release/simulator/nexuslab train \
  --file examples/training/telemetry-incast-full.yaml \
  --telemetry-summary-json > /tmp/nexuslab-incast-full-summary.json

~/.cache/nexuslab-build/release/simulator/nexuslab train \
  --file examples/training/telemetry-incast-full.yaml \
  --telemetry-records-jsonl > /tmp/nexuslab-incast-full.jsonl

~/.cache/nexuslab-build/dev/simulator/nexuslab_tests \
  --gtest_filter=TelemetryValidationTest.FullTraceRebuildMatchesTheLiveCanonicalSummary
```

The 2026-09-10 Release run completed at simulated time 41,736 ns. The summary- and full-mode runs
had equal terminal metadata, catalog definitions, metric series, and job attributions. The full
JSONL artifact contained 21,392 records and 991 samples; its complete footer digest was
`b0b5416051639062`. Canonical summary documents as wholes are intentionally not equal across modes
because provenance includes the selected telemetry configuration and source scenario digest. The
separate full-trace reconstruction test proves byte-identical live/rebuilt output under the same
full configuration.

## Abstraction and scope review

[ADR-011](../adr/ADR-011-telemetry-observability-boundary.md) preceded implementation. A per-run
session owns observation state and provides a non-owning synchronous sink. Domain snapshots remain
authoritative. The event queue owns time/order, Router and SchedulingPolicy own decisions, transport
owns queues/transfers, collectives own round execution, and workloads own job lifecycle. Telemetry
observes their post-transition facts and has no feedback path into any of them.

`summary` is the default because it preserves essential aggregate evidence without high-volume
retention. `sampled` adds bounded counter/gauge samples and policy decisions. `full` retains all
domain/kernel observations for audit and reconstruction. `off` is explicit and reserved for
controlled overhead/scale comparisons. This answers the “are we recording too much?” checkpoint:
the expensive detail is opt-in and bounded, while enabled modes do not silently truncate.

The essential research-facing set is job completion, scheduling wait, GPU idle, communication and
synchronization attribution, maximum queue occupancy, link utilization, drops by reason, transfer
outcomes, and policy decisions. Units are explicit integer quantities; timestamps share simulation
nanoseconds across subsystems. Future parallelism, rail, trace-provenance, and shadow-audit fields
require versioned additive extensions and are not claimed here.

## Required gate answers

- **Are metric names, units, and versioning stable? YES.** Catalog version 1 fixes 45 definitions;
  semantic changes require a catalog-version decision, and canonical layout changes require a
  schema-version decision.
- **Does a full-trace rebuild equal the live summary? YES.** The complete retained record stream
  reconstructs metrics and attributions, and canonical summary bytes match under identical metadata.
- **Is telemetry observational? YES.** All four modes produce the same version-1 domain digest;
  no telemetry action consumes event IDs/RNG or invokes domain behavior.
- **Are retention failures explicit? YES.** Every enabled limit is positive, checked before
  mutation where applicable, and raises a run-visible error rather than returning a complete-looking
  truncated result.
- **Can NexusBench freeze a metric catalog without UI code? YES.** The static C++ catalog and its
  canonical serialized definitions contain names, kinds, units, labels, aggregations, missing-data
  rules, and version; UI code is a consumer. NexusBench-v0 itself remains unfrozen until Cluster 24.

## Accepted limitations and next cluster

The logical telemetry files are caller-selected streams, not durable result packages. There is no
repository-defined directory layout, compression, random-access index, database, live streaming,
production telemetry ingestion, dashboard, or cluster control. Cluster 12 owns persistence and
replay packaging; Cluster 13 consumes completed artifacts only. Failure metric IDs are reserved but
detection/recovery behavior is not implemented or measured until Cluster 9.

The overhead numbers are a first host-local baseline, not portable CI thresholds or real-cluster
performance claims. Full mode's roughly 10× median peak RSS and 31.85× elapsed time on the short
record-dense case make it an audit mode, not the scale default. The 2,048-GPU stretch target was not
required for this gate and remains a later scale measurement.

Cluster 8 is complete and approved. Milestone 6 remains open. Next: Cluster 9 Failure Injection and
Recovery, including the required observable spine-link failure and recovery scenario.
