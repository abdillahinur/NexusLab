<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-011: Deterministic telemetry and observability boundary

## Status

Accepted — 2026-09-09. Implementation and validation remain part of Cluster 8.

## Context

Clusters 1–7 expose useful but separate observations: the simulation kernel has an optional event
trace, transport owns queue and transfer statistics, routing and scheduling retain decision records,
and the workload and collective engines retain timelines and terminal snapshots. The training CLI
assembles some of these values directly after a run. These surfaces prove local correctness, but
they do not yet provide one versioned metric catalog, bounded time series, cross-subsystem
correlation, or a summary that can be checked against a complete domain trace.

Cluster 8 must make important slowdowns measurable without moving domain ownership into a logging
system. It also has to preserve deterministic outcomes when telemetry detail changes and avoid
building Cluster 12's result store or replay container early. The first release remains synthetic
and replay-only; no live streaming, external monitoring backend, or cluster control is introduced.

## Decision drivers

- Identical scenarios must produce identical domain outcomes in every telemetry mode.
- Job delay must be attributable to scheduling, compute, communication, synchronization,
  queueing, drops, failures, policy choices, or configured stragglers.
- Metric names, types, units, labels, histogram boundaries, and schema versions must be explicit.
- Full traces must be sufficient to rebuild and verify their summaries.
- Scale runs need low-overhead modes and deterministic memory limits.
- Existing subsystem snapshots remain authoritative runtime state; telemetry is an observer.
- Protobuf, compression, durable result packaging, and dashboard transport remain Cluster 12 work.

## Options considered

### Ownership

#### Let each subsystem serialize its own metrics

This minimizes initial integration, but duplicates schema, versioning, retention, and correlation
logic. Cross-subsystem attribution would depend on post-processing several incompatible logs.

#### Put metrics directly in the simulation engine

The engine can see event metadata, but it cannot interpret jobs, transfers, routes, placements, or
collective phases without violating the domain boundary established in earlier gates.

#### Use one per-run telemetry session fed by typed domain observations

This keeps the event engine and domain runtimes authoritative while centralizing aggregation,
retention, schema validation, and export. This option is selected.

### Collection detail

#### Boolean tracing switch

The existing kernel switch is cheap and deterministic, but it cannot distinguish summaries from
time series or decision/domain traces.

#### Four explicit telemetry modes

`off`, `summary`, `sampled`, and `full` provide a measurable progression in cost and information.
This option is selected.

### Time-series sampling

#### Schedule telemetry events in the simulation queue

Sampling would have exact timestamps, but it would consume event IDs, change trace hashes and event
counts, and could perturb equal-time ordering.

#### Sample piecewise-constant observed state outside the event queue

The telemetry session advances only when a domain observation is emitted. Before accepting a later
timestamp, it emits elapsed interval boundaries from the prior stable state; a boundary equal to a
transition timestamp is deferred until all observations at that timestamp have been applied. Run
finalization flushes the final eligible boundary. This option is selected because sampling adds no
simulation events and cannot change scheduling.

### Serialization

#### Introduce Protobuf now

This would anticipate the replay format, but contradicts the accepted deferral to Cluster 12.

#### Versioned canonical JSON summary and JSON Lines records

Text output is inspectable, testable, and usable by later analysis without choosing the durable
replay container. This option is selected for Cluster 8. Cluster 12 may encode the same logical
schema in Protobuf and add compression without changing metric meaning.

## Decision

### Per-run session and sink boundary

Each experiment owns exactly one `TelemetrySession`. It owns the metric registry, live summary
builder, optional retained records, optional samples, deterministic record sequence, and telemetry
status. It is not global and is not reused or reset for another run. Constructing a new run creates
a clean session, which makes cross-run leakage impossible by ownership.

Subsystems emit typed value observations synchronously on the simulation thread through a narrow
non-owning `TelemetrySink`. The sink cannot schedule or cancel events, draw randomness, mutate
topology or runtime state, or call policies. It receives the post-transition simulated timestamp,
the current event ID and cause when available, typed domain identifiers, and the changed values.
The disabled implementation performs an inline mode check and does not allocate or inspect domain
payloads.

Existing subsystem snapshots and decision buffers remain available during migration. Cluster 8
adapters may translate them, but the completed integration must have one emission point per logical
transition. Polling snapshots must not create duplicate counter increments.

Telemetry records are not simulation `EventPayload` alternatives. Collection must not enlarge the
event envelope, consume event IDs, change event priority, or add work to the event queue.

### Modes

| Mode | Incremental summary | Decisions | Periodic samples | Domain transition trace | Kernel event trace |
|---|---:|---:|---:|---:|---:|
| `off` | No | No | No | No | No |
| `summary` | Yes | Counts only | No | No | No |
| `sampled` | Yes | Yes | Yes | No | No |
| `full` | Yes | Yes | Yes | Yes | Yes |

Any enabled mode records immutable run provenance and terminal simulation status. Required terminal
job and transfer counts are part of the summary. `off` retains only existing domain results needed
by callers and does not label them as telemetry.

Changing mode or sample interval must not change job, collective, route, placement, transport, RNG,
or failure outcomes. Kernel trace hashes exist only in `full`; a separate canonical domain-outcome
digest is compared across all four modes in tests and benchmarks.

### Metric catalog

Metrics are registered from a compile-time catalog. A definition contains:

- a stable numeric metric ID and lower-snake-case external name;
- schema version and description;
- kind: monotonic counter, instantaneous gauge, fixed-boundary histogram, or derived summary;
- unit: `count`, `bytes`, `chunks`, `nanoseconds`, `bits_per_second`, or
  `ratio_parts_per_million`;
- allowed bounded label dimensions and aggregation rule;
- histogram boundaries when applicable.

Hot-path lookup uses stable IDs and typed bounded label values, not arbitrary user strings. Duplicate
IDs or names, invalid units, unknown labels, non-increasing histogram boundaries, counter decrease,
or arithmetic overflow fail explicitly. Metric values and serialized timestamps are unsigned
integers. Ratios use parts per million; floating-point formatting is excluded from canonical output.

The initial required catalog covers:

- job completion, scheduling wait, compute GPU time, communication/block time, synchronization
  wait, configured straggler delay, GPU idle time, terminal outcome, and completed steps;
- collective duration and planned, local, fabric, and delivered bytes;
- link accepted, serialized, marked, and reason-specific dropped bytes/chunks;
- queue waiting bytes/chunks, maximum queue occupancy, serializer busy time, and link utilization;
- route and placement decision counts, outcome, policy/version, score and locality fields;
- simulation event counts, final time, RNG draws, and terminal status;
- failure-state transition, affected-job/traffic, detection, and recovery fields reserved for
  Cluster 9 records.

Percentiles are calculated from versioned fixed-boundary histograms and are reported as bucket
upper bounds, not fabricated interpolations. A value belongs to the first bucket whose inclusive
upper bound is at least the value; one final overflow bucket holds larger values.

### Correlation and record order

Every retained telemetry record receives a session-owned monotonically increasing `record_id` in
emission order. A correlation value contains optional typed references for event and cause IDs, job,
collective, transfer, chunk, directed link, route decision, placement decision, and failure. Fields
that are not meaningful are absent rather than represented by sentinel zeroes.

Child work carries the most specific available parent identities. In particular:

- workload records link job to collective;
- collective records link collective to its job and issued transfers;
- transport records link transfer and chunk to their collective/job when an owner exists;
- decision records link their stable decision sequence to the request and resulting transfer or
  allocation;
- failure records link affected resources and subsequent dropped work or job outcome.

Records are ordered by `(timestamp_ns, record_id)`. Equal-time order is the synchronous emission
order after the domain transition. The collector validates nondecreasing timestamps. This ordering
is deterministic and does not claim causality merely from timestamp equality.

### Summary construction and attribution

One `SummaryBuilder` consumes every typed observation before optional retention. `summary` mode
therefore uses the same state machine as `full` without storing records. An offline builder can
consume a complete full trace. Its canonical summary must equal the live summary byte-for-byte.

Duration attribution uses explicit state intervals rather than subtracting overlapping totals.
Scheduling wait ends at allocation. After allocation, each job's wall-clock critical path is
partitioned into compute, communication, synchronization/barrier, configured straggler delay, and
terminal/other intervals. GPU-time metrics remain separate additive resource-time measures. Queue
delay is attributed from arrival-at-link to serialization start. Dropped or failed work carries a
reason; it is not counted as successful communication.

Link utilization is represented by `busy_time_ns` and `observation_interval_ns`, with a derived
integer `ratio_parts_per_million`. No denominator produces no ratio. Headline summaries retain job
completion time, GPU idle time, queue depth, link utilization, and drops, and also expose scheduling
wait and successful/failed transfer counts.

### Sample semantics

Sample intervals are positive integer nanoseconds. The configuration defines a maximum number of
series and samples. Gauges are piecewise constant between observations. When observation time moves
from `T` to a later time, the collector emits all interval boundaries before the later transition
using the stable state after all observations at `T`. A boundary exactly equal to the new transition
time is emitted only after that timestamp is complete, either when time advances again or at run
finalization. Empty periods retain the last known gauge values; counters remain cumulative.

Samples use deterministic series ordering by metric ID and typed labels. Sampling is for inspection
and replay visualization; live summaries are always built from transitions, never reconstructed
from lossy samples.

### Bounds and failure behavior

Telemetry configuration validates positive limits before the run and separately bounds:

- metric series;
- retained decision records;
- retained domain records;
- retained samples;
- histogram boundaries and buckets;
- correlation edges;
- serialized bytes.

An enabled mode never silently drops required data. Exceeding a configured limit, record-ID
exhaustion, timestamp regression, unknown metric, or numeric overflow fails the simulation with an
explicit telemetry error and preserves the valid prefix for diagnosis. Scale runs reduce mode or
increase an explicit limit; they do not accept an apparently complete truncated trace.

The session may release retained record capacity only after ownership has transferred to the result
builder. Cluster 8 does not add unbounded queues, background threads, networking, or process-global
registries.

### Versioning and output

The logical telemetry schema begins at version 1. Canonical output contains:

- schema name and version;
- simulator version, synthetic-data marker, seed, scenario/configuration digest, and policy names
  and versions;
- telemetry mode, sample interval, catalog version, and configured limits;
- terminal status and completeness indicator;
- metric definitions followed by canonical summary values;
- for JSON Lines, a header followed by records in record order and a terminal footer/digest.

Canonical JSON uses UTF-8, fixed key order, decimal integer rendering, defined string escaping, no
insignificant whitespace, and a final newline. Object member order is part of NexusLab's canonical
encoding even though generic JSON consumers must not depend on it. Compatibility tests preserve
version-1 golden files. Readers reject unknown major versions and may ignore documented additive
fields within a supported version.

Cluster 8 writes or validates canonical summary JSON and telemetry JSON Lines. It does not choose a
directory layout, database, Protobuf wire format, compression, random-access index, or retention on
disk. Those remain Cluster 12 responsibilities.

### Configuration defaults

Training scenarios gain an optional `telemetry` mapping. Absence means `summary`, preserving useful
CLI metrics without high-volume retention. The mapping accepts only the selected mode's relevant
fields, including `sample_interval_ns` and explicit limits; unknown keys are rejected consistently
with existing scenario parsing. Benchmarks must print the resolved configuration.

CLI human-readable output remains available, but values come from the summary builder. Canonical
JSON/JSONL is selected explicitly and is never mixed with human timeline text on the same stream.

## Consequences

Positive:

- One schema correlates simulation, fabric, policy, collective, scheduler, and workload behavior.
- Telemetry detail can be reduced without perturbing domain results.
- Summary/full equivalence makes aggregate claims auditable.
- Bounded storage and integer encodings preserve deterministic scale behavior.
- Cluster 12 can package the logical schema without redefining metric meaning.

Negative:

- Domain runtimes need explicit instrumentation or adapters at each state transition.
- Full mode deliberately costs memory proportional to bounded record volume.
- Canonical JSON requires a small project-owned deterministic writer and golden compatibility tests.
- Piecewise-constant samples describe the simulator's model, not hardware polling behavior.
- Failure detection and recovery metrics cannot be fully exercised until Cluster 9.

## Validation

Cluster 8 must include:

- registry tests for duplicate definitions, units, label cardinality, counter/gauge updates,
  histogram boundaries, and overflow;
- correlation tests spanning job, collective, routing, transport, and placement records;
- identical domain-outcome digests across `off`, `summary`, `sampled`, and `full`;
- byte-identical live and full-trace-rebuilt summaries;
- new-session reset tests and concurrent independent session tests;
- sampling boundary, equal-time ordering, final flush, limit, and disabled-mode tests;
- canonical JSON/JSONL golden files and supported/unsupported schema-version tests;
- successful, failed, cancelled, and unfinished-job completion-metric coverage;
- a telemetry overhead benchmark over the same scenario and seed in all four modes, reporting wall
  time, events per second, peak RSS, retained records/samples, and serialized bytes;
- GCC, Clang, clang-tidy, formatting, Release, and sanitizer verification;
- a documented metric catalog and Architecture Gate 8 review.

The benchmark establishes the first telemetry overhead baseline. Regression thresholds are set only
after that baseline is measured, following the same policy used by Cluster 1.

## Revisit triggers

Revisit this decision if:

- full-trace memory or overhead prevents the approved 512-GPU scenario from completing;
- domain outcomes differ across telemetry modes;
- a summary cannot be reconstructed from a complete trace;
- fixed histograms obscure a headline comparison;
- Cluster 9 requires delayed observations that cannot be represented as typed records;
- Cluster 12 demonstrates that JSON logical fields cannot map cleanly to the replay schema;
- live telemetry is authorized in a later release and needs asynchronous backpressure semantics.
