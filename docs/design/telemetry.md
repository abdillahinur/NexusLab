<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Telemetry design and metric catalog

Status: Cluster 8 complete; Architecture Gate 8 approved on 2026-09-10. The logical telemetry
schema remains version 1. Legacy runs use metric catalog 1; scripted runs now use catalog 2. [ADR-011](../adr/ADR-011-telemetry-observability-boundary.md)
owns the architectural decision; this document is the operator-facing contract.

## Observation boundary

Each training run owns one telemetry session. Simulation, workload, collective, routing,
scheduling, and transport components emit typed observations after domain transitions. The sink
cannot schedule events, draw randomness, invoke policies, or mutate domain state. Consequently,
telemetry mode changes observation cost and retained detail, but not modeled outcomes.

| Mode | Incremental summary | Decision records | Domain/kernel records | Samples |
|---|---:|---:|---:|---:|
| `off` | No | No | No | No |
| `summary` | Yes | No | No | No |
| `sampled` | Yes | Yes | No | Yes |
| `full` | Yes | Yes | Yes | Yes |

Every enabled mode sends the same typed observations through the same summary builder. Full mode
also retains the complete typed stream in `(timestamp_ns, record_id)` order. A record ID is a
per-session monotonic sequence. Sampling is observational and piecewise constant: it consumes no
simulation event IDs, and an equal-time boundary is emitted only after that timestamp is complete.

## Version and canonical output contract

Canonical summary JSON uses schema `nexuslab.telemetry.summary`; JSON Lines uses
`nexuslab.telemetry.records`. Both currently require `schema_version: 1` and embed
`catalog_version: 1` for legacy runs or `2` for scripted runs. Output is UTF-8 with fixed key order, decimal integers, defined escaping, no
insignificant whitespace, and a final newline. Unknown major schema versions are rejected.

Both forms identify the simulator version, synthetic-data status, seed, scenario digest, routing
and optional scheduling policies and versions, resolved telemetry configuration, terminal status,
completeness, and final simulated time. Summary JSON then contains the catalog, metric series, and
job attributions. JSON Lines contains a header with the same provenance and catalog, ordered
records, ordered samples, and a footer with record/sample counts plus a content digest.

This logical payload is sufficient for a later replay UI to display provenance, terminal and
missing state, aggregate metrics, time series, policy decisions, and correlation chains without
defining metric meaning in UI code. Cluster 12 still owns durable result packages, file layout,
compression, indexes, and replay-container compatibility. There is no live streaming or cluster
control in the first release.

## Metric dictionary rules

Catalog names use lowercase letters, decimal digits, and underscores. IDs, names, kinds, units,
allowed labels, and histogram boundaries are versioned together. Labels are typed numeric values;
at most four distinct labels may identify one series. The current policy label values are ECMP 1,
shortest-path 2, least-loaded 3, queue-aware 4, first-fit 10, random 11, rack-local 12, compact 13,
and extension 1000. Outcome values remain the numeric value of their producing typed domain enum,
not free-form text.

Counters are the checked sum of emitted increments. Gauges are the last value at or before the
observation time. Histograms store a checked count and sum plus fixed inclusive-upper-bound
buckets; the final bucket is overflow. Derived summaries state their formula explicitly. Series
are ordered by metric ID and then typed labels.

An absent series means no valid observation was emitted for that exact metric/label set. Consumers
must not silently impute it or interpolate a percentile. A missing histogram has no percentile; a
histogram with count zero has no percentile. An unfinished job has attribution with `terminal=false`
and no completion histogram or terminal counter. Samples omit series that did not yet exist at the
boundary; absence is not a sampled zero. Failed or incomplete result cells must remain explicit.

Latency histogram boundaries, in nanoseconds, are `1000`, `10000`, `100000`, `1000000`,
`10000000`, `100000000`, `1000000000`, `10000000000`, and `60000000000`. Placement-count
boundaries are `0`, `1`, `2`, `4`, `8`, `16`, `32`, `64`, `128`, `256`, `512`, `1024`, `2048`,
and `8192`.

## Catalog version 1

`Labels` lists required dimensions first; a parenthesized dimension is allowed but optional.

| ID | Metric name | Kind | Unit | Labels | Aggregation |
|---:|---|---|---|---|---|
| 1 | `simulation_dispatched_events_total` | counter | count | — | Sum dispatched events |
| 2 | `simulation_cancelled_events_total` | counter | count | — | Sum cancelled events |
| 3 | `simulation_final_time_ns` | gauge | nanoseconds | — | Last final timestamp |
| 4 | `simulation_rng_draws_total` | counter | count | — | Sum deterministic RNG draws |
| 5 | `simulation_terminal_total` | counter | count | outcome | Sum by terminal outcome |
| 10 | `job_completion_time_ns` | histogram | nanoseconds | — | Latency histogram per terminal job |
| 11 | `job_scheduling_wait_ns` | histogram | nanoseconds | — | Latency histogram per terminal job |
| 12 | `job_compute_gpu_ns` | counter | nanoseconds | — | Sum allocated GPU compute resource-time |
| 13 | `job_communication_ns` | counter | nanoseconds | — | Sum job critical-path communication intervals |
| 14 | `job_synchronization_wait_ns` | counter | nanoseconds | — | Sum critical-path synchronization intervals |
| 15 | `job_straggler_delay_ns` | counter | nanoseconds | — | Sum configured critical-path straggler intervals |
| 16 | `job_gpu_idle_ns` | counter | nanoseconds | — | Sum allocated idle GPU resource-time |
| 17 | `job_completed_steps` | counter | count | — | Sum completed training steps |
| 18 | `job_terminal_total` | counter | count | outcome | Sum terminal jobs by outcome |
| 30 | `collective_duration_ns` | histogram | nanoseconds | — | Latency histogram per terminal collective |
| 31 | `collective_planned_bytes_total` | counter | bytes | — | Sum planned logical collective bytes |
| 32 | `collective_issued_fabric_bytes_total` | counter | bytes | — | Sum collective bytes issued to fabric |
| 33 | `collective_issued_local_bytes_total` | counter | bytes | — | Sum collective bytes issued locally |
| 34 | `collective_delivered_bytes_total` | counter | bytes | — | Sum delivered collective bytes |
| 50 | `link_accepted_bytes_total` | counter | bytes | link, direction | Sum bytes accepted by each directed queue |
| 51 | `link_accepted_chunks_total` | counter | chunks | link, direction | Sum chunks accepted by each directed queue |
| 52 | `link_serialized_bytes_total` | counter | bytes | link, direction | Sum fully serialized bytes |
| 53 | `link_serialized_chunks_total` | counter | chunks | link, direction | Sum fully serialized chunks |
| 54 | `link_marked_bytes_total` | counter | bytes | link, direction | Sum congestion-marked bytes |
| 55 | `link_marked_chunks_total` | counter | chunks | link, direction | Sum congestion-marked chunks |
| 56 | `link_dropped_buffer_full_bytes_total` | counter | bytes | link, direction | Sum buffer-full dropped bytes |
| 57 | `link_dropped_buffer_full_chunks_total` | counter | chunks | link, direction | Sum buffer-full dropped chunks |
| 58 | `link_dropped_down_bytes_total` | counter | bytes | link, direction | Sum resource-down dropped bytes |
| 59 | `link_dropped_down_chunks_total` | counter | chunks | link, direction | Sum resource-down dropped chunks |
| 60 | `link_queue_waiting_bytes` | gauge | bytes | link, direction | Last waiting-byte occupancy |
| 61 | `link_queue_waiting_chunks` | gauge | chunks | link, direction | Last waiting-chunk occupancy |
| 62 | `link_maximum_waiting_bytes` | gauge | bytes | link, direction | Maximum observed waiting-byte occupancy |
| 63 | `link_serializer_busy_ns_total` | counter | nanoseconds | link, direction | Sum elapsed serializer busy intervals |
| 64 | `link_utilization_ratio_ppm` | derived summary | ratio parts per million | link, direction | `busy_ns * 1000000 / observation_interval_ns`; absent without a denominator |
| 70 | `transfer_terminal_total` | counter | count | outcome | Sum terminal transfers by outcome |
| 80 | `routing_decision_total` | counter | count | policy, outcome | Sum routing decisions by policy/outcome |
| 81 | `placement_decision_total` | counter | count | policy, outcome | Sum placement decisions by policy/outcome |
| 82 | `placement_cross_rack_ring_edges` | histogram | count | — | Placement-count histogram per successful placement |
| 83 | `placement_rack_count` | histogram | count | — | Rack-count histogram per successful placement |
| 84 | `placement_nic_count` | histogram | count | — | NIC-count histogram per successful placement |
| 100 | `failure_state_transition_total` | counter | count | failure, resource | Sum failure lifecycle transitions |
| 101 | `failure_affected_jobs_total` | counter | count | failure, (resource) | Sum jobs attributed to each failure |
| 102 | `failure_affected_traffic_bytes_total` | counter | bytes | failure, (resource) | Sum traffic bytes attributed to each failure |
| 103 | `failure_detection_time_ns` | histogram | nanoseconds | (resource) | Detection-latency histogram |
| 104 | `failure_recovery_time_ns` | histogram | nanoseconds | (resource) | Recovery-latency histogram |

IDs 100–104 are defined so Cluster 9 can add typed failure observations without renaming the
catalog. Their absence in a Cluster 8 run means no implemented failure lifecycle observation was
emitted; it does not mean measured zero detection or recovery latency.

## Correlation and attribution

Records may carry typed GPU, NIC, switch, port, rack, directed-link, event, cause, job, collective,
transfer, chunk, routing-decision, placement-decision, and failure references. Unavailable
relationships are absent rather than encoded as zero. Equal timestamps preserve synchronous
emission order but do not alone assert causality.

Job wall-clock attribution partitions arrival-to-observation time into scheduling wait, compute,
communication, synchronization wait, configured straggler delay, and terminal/other. Allocated GPU
compute and idle values are separate resource-time metrics and must not be added to that wall-clock
partition. Queue delay comes from typed queue transitions; drops carry buffer-full or resource-down
reasons; policy records expose version, candidates, selected path or workers, score/locality,
outcome, and reason.

## Bounds and compatibility

Defaults are 100,000 metric series, 100,000 decision records, 1,000,000 domain records, 1,000,000
samples, 64 histogram boundaries per definition, 4,000,000 correlation edges, and 1 GiB per
serialized document. Every value and the sample interval must be positive. Timestamp regression,
unknown metrics, invalid labels, arithmetic or ID overflow, and any enabled retention limit fail
explicitly; required observations are never silently dropped.

Additive fields may be ignored only where a supported schema version documents that behavior.
Changing an ID, name, kind, unit, required label, aggregation, missing-data rule, or histogram
boundary requires an intentional catalog-version decision. Changing canonical layout or meaning
requires an intentional schema-version decision. NexusBench may freeze a subset of this dictionary
by catalog version without importing UI code, but NexusBench-v0 is not frozen until Cluster 24.

## Reproduction and evidence

The [training scenario guide](../training-scenarios.md) documents configuration and CLI output.
The [Cluster 8 baseline](../benchmarks/cluster-8-telemetry-baseline.md) records measured overhead.
The [Gate 8 record](../architecture-gates/cluster-8.md) maps the implementation and validation to
the acceptance criteria.

## Catalog-v2 scripted perturbations

Schema version 1 is retained. Scripted CLI exports select catalog 2; legacy runs select catalog 1
and retain their original canonical summary/JSONL bytes. require_supported_catalog_version accepts
1 and 2 and rejects other values. Reserved metric IDs, units and label masks remain unchanged;
catalog 2 activates their accepted failure semantics. Domain outcome digest layouts v1–v4 are
unchanged. Their checksums exclude observation mode and retained telemetry.

| Metric ID | Meaning | Labels |
|---|---|---|
| 100 failure_state_transition_total | One increment for each Scheduled, Applied, Detected, RecoveryStarted or Recovered transition | Failure ID and Resource kind |
| 101 failure_affected_jobs_total | One increment per distinct incident/job pair, including no-route admissions and GPU aborts | Failure ID; Resource kind included |
| 102 failure_affected_traffic_bytes_total | Uniquely attributed down-dropped logical bytes; excludes buffer-full drops, no-route admissions and slower traffic | Failure ID; Resource kind included |
| 103 failure_detection_time_ns | One zero-latency sample for each immediate detector application | Resource kind |
| 104 failure_recovery_time_ns | Configured onset-to-restoration duration; no sample for pending/permanent incidents | Resource kind |

Resource label codes are Link 1, Port 2, Switch 3 and Gpu 4. Individual resource IDs remain in typed
records. Missing recovery is represented as missing, not a zero sample. Recovered resource state
may remain effectively unavailable under another active cause; restoration never restarts jobs.

Existing Failure record kind 15 keeps transition codes Scheduled 1, Detected 2, Applied 3,
RecoveryStarted 4 and Recovered 5. New fact codes are JobAffected 6, TrafficDropped 7,
AdmissionRejected 8 and StateObserved 9. Facts do not increment metric 100. StateObserved refreshes
an incident's effective health/capacity when another cause changes it. Failure records add:
incident_kind (down 1, degraded 2, port 3, switch 4, GPU 5), onset_ns, optional recover_at_ns and
bandwidth_bps, applied/recovered/effective_operational, capacity_revision, cumulative affected_jobs,
dropped_bytes and dropped_chunks. Link incidents include baseline_capacity_bps and
effective_capacity_bps arrays ordered A-to-B then B-to-A; non-link incidents use null. Existing
fields and numeric values remain unchanged; absent additive fields retain the legacy encoding.

Load record kind 16 carries full per-request snapshots: separate traffic_source ID, zero-based
request ordinal, load_kind (burst 1/checkpoint 2), endpoint GPUs, scheduled time, count, interval,
bytes/chunk sizes, optional injection/completion times and transfer ID, outcome (Pending 1,
Succeeded 2, Failed 3, NoRoute 4), and delivered/buffer-drop/down-drop byte/chunk counts. Bootstrap
snapshots describe pending requests. NoRoute has no transfer or fabricated drops. Checkpoint is
traffic provenance, not storage/checkpoint consistency or restart semantics.

Correlations add optional traffic_source and flow fields. Incident and source IDs use separate
namespaces even when their numeric values coincide. Scripted routing records include request
flow/endpoints/bytes/chunk size and capacity_revision. AdmissionRejected facts join the actual
routing event/flow, collective and job. Background requests never create affected training jobs.
Existing transfer/chunk/link correlations connect unique failure drops to transport facts.

Summary JSON adds perturbations: latest snapshots sorted by record kind, source/incident ID and
request ordinal. Every enabled mode builds this view through the same SummaryBuilder. It is
bounded independently by limits.metric_series, in addition to metric-series limits; exhaustion
fails explicitly. Full record, decision, correlation-edge, sample and serialized-byte limits still
apply separately. No truncation or sampling is substituted for a complete full trace.

Scripted metadata adds failure_script_digest_fnv1a64, domain_outcome_digest_fnv1a64 and its version,
capacity_revision and domain_success. The latter is false for any failed, waiting or pending
job/load and is distinct from terminal.status (simulation queue outcome). domain_observations_complete
is false in off mode. records_complete is true only for a completed full-mode simulation; it is
false for off/summary/sampled and stopped runs. A failed job may coexist with a completed full
record stream; its domain_success remains false. The existing JSONL footer digest still hashes
all preceding header/record/sample bytes and excludes the footer itself.

reconstruct_full_trace(snapshot, metadata, catalog_version) replays finalized in-memory full
records, requires matching supported catalog provenance, validates contiguous IDs and timestamp
bounds, and rebuilds metric series, job attributions and perturbation snapshots. It does not reuse
cached summary values. Legacy and scripted canonical summaries reconstruct byte-for-byte.
Stopped full traces can be replayed as the observed stopped run, preserving pending state; they
are not labeled as complete experiments. JSONL parsing/import is a separate future boundary.
