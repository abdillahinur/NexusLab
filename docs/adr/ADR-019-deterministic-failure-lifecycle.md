<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-019: Deterministic failure scripts, detection, and recovery

## Status

Accepted — 2026-11-22 after contract review. This authorizes incremental Cluster 9 implementation;
it does not record complete implementation or Gate 9 approval. ADR-012 through ADR-018 retain their reserved
numbers. The [execution plan](../design/failure-injection.md) defines the implementation slices and
acceptance evidence. The master plan's complete Cluster 9 scope remains required.
Link/port/switch/GPU incidents, capacity degradation and bounded background loads are implemented.
No-route job attribution and catalog-v2 serialization/reconstruction are implemented. Scripted
JSON/JSONL export is enabled with explicit domain outcomes and observation completeness. Controlled
policy comparisons, overhead evidence and Gate 9 approval remain pending.

## Context

Clusters 2–4 already implement physical link/port/switch state, transport reconciliation and
admission-time route invalidation. Cluster 7 has persistent GPU down/up controls in scheduler mode;
legacy worker_failure only aborts a specified job. Cluster 8 reserves failure IDs, observations,
and metrics, but there is no scenario-owned failure lifecycle. A graph state setter alone does not
supply reproducible scripts, detection timing, affected-job attribution, or a recovery timeline.

This decision composes those owners. It does not replace ADR-006's loss model, ADR-007's fixed
admitted routes, ADR-010's job ownership, or ADR-011's observational telemetry. Resource restoration
must not imply successful completion of traffic or jobs that previously failed.

## Options considered

- **Direct graph mutation:** small initial change, but bypasses queue cancellation, accounting,
  route-cache revisions, and job ownership. Rejected during simulation.
- **A per-run coordinator using existing runtime boundaries:** one normalized script and stable
  lifecycle, with transport, workload and routing retaining domain ownership. Selected.
- **Delayed detection and transparent retries now:** could support richer recovery comparisons,
  but requires stale policy views, timers, acknowledgments and reliable transport semantics.
  Deferred; it cannot be simulated by postponing physical failure until detection.

## Decision

### Ownership and scope

A single-use FailureRuntime owns normalized incident records, scheduled action IDs, incident state,
and bounded attribution sets. It borrows the topology, transport and workload owners; their
lifetimes exceed its own. It does not own queues, allocate GPUs, choose routes, consume random
draws, or let telemetry drive simulation. TrainingDispatcher invokes it and retains exclusive
ownership of transfer/collective outcome forwarding.

Full Cluster 9 includes link down/up, reduced link capacity, switch down/up, persistent GPU worker
failure/recovery, traffic bursts and synthetic background checkpoint traffic. Ports use the same
incident lifecycle and reconciliation already supported by transport. NIC/rail/domain failures,
silent loss, stale telemetry, production fault injection and automatic job restart remain excluded.

A burst or checkpoint source is a workload perturbation, not a failed physical component. It has
a separate TrafficSourceId and correlation; it must not manufacture link-down/detection metrics.
Checkpoint traffic models explicit bytes over endpoints, not storage, checkpoint consistency,
training-state restoration or disk performance. Both load kinds use the same bounded injection
mechanism and differ only in declared provenance and request schedule.

### Scenario contract

Existing training scenario version 1 gains an optional failure_script mapping. Its absence preserves
existing scheduling, output and digest behavior. Unknown/duplicate keys, unsupported versions and
invalid targets fail before event registration. The full contract is implemented incrementally; unsupported kinds and loads must be rejected.
The following illustrates the schema; target IDs must be resolved against the generated topology before acceptance:

```yaml
failure_script:
  version: 1
  detection: immediate
  incidents:
    - kind: link_down
      target: 64
      at_ns: 15000
      recover_at_ns: 30000
  loads: []
```

Incident fields are kind, target, at_ns, optional recover_at_ns, and bandwidth_bps only for
link_degraded. Valid kinds are link_down, link_degraded, port_down, switch_down and gpu_down.
Targets are physical LinkId, PortId, SwitchId or GpuId according to kind; link incidents affect both
fabric directions. Local GPU–NIC attachments are not fabric failure targets. Link degradation uses
a positive absolute capacity below both baseline direction capacities; recovery restores each
direction's own baseline. Buffer and propagation parameters do not change.

Incident IDs are one-based declaration ordinals. No caller-supplied ID, implicit random target,
random failure timing, or automatic retry is allowed. Reordering declarations intentionally changes
identity and digest. Omitted recovery means the impairment remains active at queue exhaustion;
otherwise recover_at_ns must be strictly greater than at_ns. All times are absolute unsigned
integer nanoseconds with checked arithmetic. Overlapping episodes on the same physical target are
rejected, including down versus degraded. Adjacent intervals are allowed. Different resources may
overlap, including a link whose switch or port is down.

A load entry has kind (burst or checkpoint), source_gpu, destination_gpu, at_ns, count,
interval_ns, bytes_per_transfer and chunk_bytes. Its TrafficSourceId is its one-based load ordinal.
Endpoints must exist, differ, and use different NICs; this load generator models fabric traffic.
Count, byte and chunk sizes are positive; interval_ns may be zero. Request k occurs at
at_ns + k * interval_ns for k in [0, count). All bounds and arithmetic are checked before the run.
Load transfers use the selected Router and real transport outcomes, with a dedicated owner in the
dispatcher; they are never forwarded to RingExecutor or mislabeled as training collectives.

Persistent gpu_down requires scheduler mode, including for pinned jobs, so one GPU inventory remains
authoritative. Failure scripts may coexist with legacy job cancellation/worker_failure controls;
those retain job-scoped meaning and terminal notifications remain exactly once. A gpu_controls entry
that targets a scripted GPU is rejected to prevent competing persistent-state owners. Direct
low-level state events on scripted resources are forbidden in the composed scenario runner.

Defaults bound scripts to 10,000 incidents, 10,000 load sources and 100,000 generated load requests.
The existing one-MiB scenario bound still applies. Separate positive limits bound affected-job
entries and attributed chunk entries to 1,000,000 each. Preflight checks derived action/request
counts and all limits before consuming event IDs; normal transport/telemetry limits still apply.
Memory, event-ID or retention exhaustion fails the run explicitly; rollback/resume is not promised.

### Event ordering and lifecycle

Bootstrap registers the complete failure calendar before existing jobs, job controls and GPU
controls. Calendar actions use Critical priority and are scheduled in the order
(timestamp, action phase, FailureId), with recovery phase 0 and onset phase 1. Both lifecycle and
reconciliation execute synchronously in that action's handler; detection is not a follow-up event.
The link-only slice reuses LinkStateChangeEvent, with the coordinator validating its registered
event ID, target, state and timestamp. It adds no event alternative or trace kind. Future action
types add a payload only when existing typed events cannot express them.
Existing equal-priority controls follow failure actions by event ID. Load requests use Normal
priority and are registered after the existing workload events, in (timestamp, TrafficSourceId,
request ordinal) order. No priority values or kernel comparator semantics change.

Thus restoration is processed before a new impairment at the same time; all scripted operational
changes precede Control transmission completions and Normal arrivals. Failure at the exact
serialization-completion timestamp cancels that completion and drops the whole active chunk, as
ADR-006 already requires. New small value-owned event payloads contain IDs/action tags, not paths,
strings or pointers. Existing trace-kind numbers remain stable; new kinds receive explicit IDs.

Each incident emits Scheduled at bootstrap time zero. At onset, apply and reconcile the
physical effect atomically, then emit Applied followed by Detected at the same timestamp. Their telemetry emission
order is explicit; enum numeric values do not prescribe order. immediate is the only v1 detector;
any delayed detector or delay field is rejected rather than ignored. Detection latency is zero,
and policy observations reflect actual current health after the handler returns.

At recover_at_ns, emit RecoveryStarted, restore this incident's baseline state through its owner,
then emit Recovered. Recovered means this incident was cleared, not that the whole path is usable
or that a failed job restarted. Incident snapshots retain effective_operational after reconciliation
so a recovered link blocked by another down resource is explicit. Unrecovered incidents have no
recovery observation or latency sample. A run stopped before an action retains it as pending;
there is no fabricated detection, recovery or completion.

Restoration updates the targeted state only. A down switch/port continues to block a recovered
link. Existing topology operational revisions invalidate cached path sets; no-route is an explicit
routing outcome. Only new admissions can select another path. There is no mid-flight route repair,
retransmission, re-planning of failed collectives, or restart of failed jobs.

### In-flight traffic and capacity degradation

For down transitions, use existing transport reconciliation: cancel active completion events,
close busy-time intervals, drop whole active and queued chunks with existing down reasons, and
empty affected serializers. Already propagating chunks are not recalled; they check the next arc
on arrival. Retain byte conservation and let other admitted chunks drain. Transfer, collective and
job outcomes remain distinct from simulation queue exhaustion.

Degradation is a transport service-configuration change, not a topology down event. An active chunk
finishes using the bandwidth and propagation settings captured when it began serialization.
Waiting chunks and later admissions use the effective capacity when they start service. Restoration
uses the same rule, so it does not accelerate an active chunk. This explicitly extends ADR-006's
immutable per-direction configuration; no implementation may silently mutate a const view or
reinterpret prior busy time. The service must retain the active timing snapshot. Preflight serialization durations under all
configured capacities; check actual completion and propagation timestamps again at service start.
Waiting time cannot be predicted by static validation; runtime arithmetic failure terminates the run.

Transport exposes effective capacity through its read-only FabricView. Queue-aware scoring uses
that capacity; topology reachability and operational_revision need not change for degradation.
A separate monotonically increasing capacity_revision is recorded in decisions and incident
snapshots when effective capacity changes. Rebuilding shortest paths is unnecessary for a capacity
change. No current controller is allowed to rewrite the configured physical baseline.

GPU failure persists in the existing inventory, aborts the owner once, releases its entire allocation
and leaves that GPU unhealthy. Previously issued local/fabric traffic drains under existing rules.
Recovery makes the GPU eligible for subsequent admission; it never resumes the aborted job.
Queue exhaustion with blocked waiters produces explicit unfinished outcomes and a nonzero CLI exit.

### Affected work and telemetry

At each onset, inspect active/waiting chunks on newly unavailable arcs and attribute their drops to
that incident. Later down-arrival drops use the lowest active FailureId blocking that arc. A chunk
contributes its full logical size once to exactly one incident, never once per hop; reason-specific
transport counters remain authoritative. Jobs reached through those transfer/collective owners,
or directly aborted by gpu_down, contribute once per (FailureId, JobId). Independent incidents may
affect the same job. Unreachable admissions can affect a job without adding dropped bytes because
no chunk was admitted. Already terminal jobs and buffer-full drops are not retrospectively blamed.
For a no-route decision, attribute to the lowest active down incident that disables an arc on a
healthy-topology minimum-hop candidate; if none qualifies, record no incident attribution. This is
a deterministic attribution rule, not proof that any single incident was the sole cause.

Collective no-route admissions retain the collective ID, fabric endpoints and timestamp until the
training dispatcher resolves ownership after submit returns and before consuming terminal results.
There is at most one such admission per failed collective, bounded by maximum_collectives; the
composition drains admissions on each outcome-pump pass even without a failure runtime. Two
structural fabric BFS traversals classify every healthy minimum-hop arc without enumerating or
truncating route candidates. Current active link/port/switch causes are checked on those arcs;
capacity degradation and GPU health do not qualify as fabric down causes. Recovered incidents,
unrelated/nonminimum-path failures and already terminal/unowned jobs add no affected-job pair.
The existing affected-job bound applies, and exhaustion fails the simulation explicitly. No new
transfer, dropped bytes or dropped chunks are fabricated. Background requests retain their own
no-route outcomes without becoming jobs. Catalog-v2 admission facts join the actual routing
decision, collective and job through flow/event correlations; background sources retain a separate namespace.

This completes an existing affected-job field's semantics. No digest field or encoding changes:
v1/v2/v3/v4 layouts and script goldens remain fixed; newly attributed runs correctly hash the
additional affected-job pair through the existing encoding.

Link degradation contributes zero to the existing failure_affected_traffic_bytes_total: that metric
counts failure-attributed dropped bytes, not slowed traffic volume. No incident-linked affected-job
count is inferred from timing differences. GPU-aborted jobs count directly, while their draining
traffic is not counted as dropped. Extra exposure/slowdown metrics require separate definitions.

Reserved metric IDs 100–104 retain their numeric IDs and units. Define:

- failure_state_transition_total: one increment per emitted incident lifecycle transition;
- failure_affected_jobs_total: one increment per distinct affected (incident, job) pair;
- failure_affected_traffic_bytes_total: sum of uniquely attributed down-dropped logical chunk bytes;
- failure_detection_time_ns: Detected minus Applied, one sample per detected incident;
- failure_recovery_time_ns: Recovered minus Applied, one sample per cleared incident, including the
  configured impaired interval; it is not queue drain time or application recovery latency.

These exact semantics activate the previously reserved definitions under metric catalog version 2.
On completion of the telemetry slice, scripted runs select catalog version 2; legacy runs without
failure_script retain catalog version 1
and their original bytes. Both catalogs must remain available for reconstruction, with explicit
version selection and golden fixtures; version 1 artifacts remain immutable. Preserve telemetry logical schema version 1 using documented optional
additive incident/health/capacity fields; absent fields retain the old encoding. FailureObservation
extensions must preserve existing numeric resource/transition values. Full traces must reconstruct
live summaries byte-for-byte; off/summary/sampled/full must preserve identical domain outcomes.
Failure metric labels use the incident ID and resource-kind code (Link 1, Port 2, Switch 3, Gpu 4);
per-target resource IDs remain in typed records. Required labels retain existing masks, and latency
histograms consistently include the optional Resource kind label. Reasons stay typed and bounded. Missing detection/recovery samples mean missing,
not zero. Telemetry limits apply independently from FailureRuntime attribution limits.

### Script identity and comparison boundary

A version-1 failure_script_digest uses the existing project-owned 64-bit FNV-1a algorithm over a
fixed binary encoding: length-prefixed UTF-8 domain nexuslab.failure-script, u64 version 1,
length-prefixed canonical topology YAML, detector code 1, incident count, incident records in
FailureId order, load count, then load records in TrafficSourceId order. Integers, string lengths
and optional-presence flags are u64 little-endian. Booleans/presence flags are 0 or 1. Kind codes
are link_down 1, link_degraded 2, port_down 3, switch_down 4, gpu_down 5; load codes are burst 1 and
checkpoint 2. Each incident encodes ID, kind, target, onset, recovery-presence, optional recovery,
capacity-presence, optional capacity, in that order. Each load encodes ID, kind, source GPU,
destination GPU, onset, count, interval, bytes and chunk size, in that order. Render 16 lowercase
hex digits. No raw YAML whitespace, policy name, telemetry mode, memory address or std::hash enters
this digest. It is a reproducibility checksum, not a security primitive or NexusBench receipt.

Bounds and baseline/effective transport parameters remain recorded scenario inputs, independently
of this script digest. Comparison tests must hold complete resolved topology, workload, placement,
transport parameters, seed and script fixed and vary only routing. Equal script digests alone do
not prove controlled experiments. The domain outcome digest remains version 1 for legacy runs; link-only scripts retain version 2,
and resource/capacity scripts use version 3 with explicit revision metadata. Incident state,
effective health and attribution are domain facts even with telemetry off. Load scripts use outcome
version 4: retain the complete v3 prefix with the resolved version set to 4, then append the load
request count and records in source/ordinal order. Each record encodes source ID, zero-based request
ordinal, scheduled time, injected-presence/optional time, transfer-presence/optional ID, outcome
(Pending 1, Succeeded 2, Failed 3, NoRoute 4), finished-presence/optional time, then delivered,
buffer-drop and link-down-drop byte/chunk pairs. All values use u64 little-endian. Source
specifications are already covered by the script digest. Preserve old v1/v2/v3 encodings.

The background owner reserves flow keys with bit 63 set; collective flow allocation is restricted
to the lower half, preserving all prior keys. Typed injection event kind 10 is appended without
renumbering prior variants or trace kinds. Bootstrap timing checks use independent path lookup and
the transport's minimum reserved capacities; they consume no routing decisions, IDs or RNG draws.
Actual admissions validate timing again because outages can change paths and queue waits. The
finite-buffer transport supplies drops; no separate generator backpressure or retry is invented.

## Consequences

The first spine-link demonstration can compare admission routing under identical impairments,
including failed jobs. It cannot compare two recovery algorithms or promise eventual delivery.
Immediate detection is a documented simplification; delayed/stale views need a later ADR extension.
Bounded correlation and load ownership add runtime state and event costs that must be measured.
Durable result packaging, matrix commands and dashboards remain Clusters 11–13 work.

## Validation and revisit triggers

The [acceptance matrix](../design/failure-injection.md#acceptance-matrix) is required for Gate 9.
Review this decision before adding delayed detection, overlapping same-target incidents, retries,
mid-flight bandwidth rescheduling, NIC/rail failures, or recovery-policy comparisons. Event-envelope
or material queue/pending-state changes require the existing Gate 1 measurements. No placeholder
metrics, unexecuted demo or architecture document constitutes gate evidence.

## Resource/capacity refinement — 2026-11-22

The coordinator now normalizes targets as a variant of strong LinkId, PortId, SwitchId and GpuId
values; kind/target mismatches are rejected. Link-down and degraded episodes share one physical
resource key, so they cannot overlap on the same link. Distinct resource kinds with equal numeric
IDs remain independent. Fabric ports are supported; local GPU–NIC attachment ports are rejected.
GPU incidents require scheduler-owned health and cannot share a GPU with legacy gpu_controls.

Link, port, switch and GPU actions reuse their existing typed events. Capacity changes introduce
LinkCapacityChangeEvent with stable trace kind 9; previous kind numbers and variant positions remain
unchanged. Its two bandwidth fields permit exact restoration of asymmetric direction baselines.
The payload fits the existing 32-byte variant and the complete Event remains 72 bytes. A service's
scheduled completion and active_bandwidth snapshot retain its start-time serialization rate.
Only future service starts use the changed queue capacity. Propagation and buffers remain fixed.

At bootstrap the transport retains the minimum scripted bandwidth per affected direction for
future admission-time arithmetic validation. An unrepresentable serialization duration is rejected
before allocating transfer IDs. Actual service-start timestamps are checked again; overflow from
unpredictable waiting time fails the run. Capacity updates validate both directions and revision
arithmetic before ordinary mutation; memory exhaustion retains the existing fatal-run contract.

Capacity-only changes increment a separate monotonic capacity_revision, once per physical-link
update, and do not change topology operational_revision or rebuild reachability candidates.
Routing decisions and incident/timeline domain snapshots retain capacity revisions. Queue-aware
scoring reads effective capacity through the existing FabricView. A recovered link remains
unavailable under a down port or switch. Later down-arrival drops select the lowest active
incident ID among the arc's link, endpoint ports and endpoint switches, without scanning all
incidents. GPU failure attributes its owner before abort/release; draining traffic is not dropped.

The version-1 script digest uses the kind/capacity slots already specified above. Existing link-only
script goldens remain unchanged. Runs with new resource kinds use domain outcome digest version 3,
which adds global, routing, incident and timeline capacity revisions; legacy no-script runs retain
version 1 and link-only scripts retain version 2. Use resolved_domain_outcome_digest_version(report).
Future fields need another explicit version decision; version 3 must not be silently extended.
Catalog-v2 failure telemetry/export and typed full-trace summary reconstruction are implemented;
legacy catalog-v1 bytes remain unchanged.
This refinement does not approve Gate 9 or introduce retries, background traffic or real actuation.

### Catalog-v2 implementation refinement — 2026-11-22

Scripted composition passes the observational sink into the failure and load owners. Lifecycle
transitions activate metrics 100–104 with the exact semantics above. Non-lifecycle facts use
additional transition codes JobAffected 6, TrafficDropped 7, AdmissionRejected 8 and StateObserved
9; none increments the lifecycle counter or domain timeline. StateObserved refreshes effective
health/capacity after another resource restores, preventing stale incident summaries.

Failure records carry additive specification/state/capacity fields; load record kind 16 carries
complete request snapshots, including pending and negative outcomes. Routing records add request
and capacity provenance only for scripted composition. Event priorities, payload kinds, domain
scheduling and digest layouts are unchanged. Catalog-v1 fixtures and record bytes remain frozen.
See the [catalog-v2 dictionary](../design/telemetry.md#catalog-v2-scripted-perturbations) for exact
field/correlation codes, completeness semantics and retention bounds. Reconstruction replays
finalized in-memory full records with explicit catalog selection; JSONL parsing/import is not
introduced here. Final comparisons, overhead characterization and Gate 9 remain separate work.
