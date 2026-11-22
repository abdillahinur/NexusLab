<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 9 implementation and acceptance plan

Status: link, resource, capacity and background-load slices implemented — 2026-11-22.
[ADR-019](../adr/ADR-019-deterministic-failure-lifecycle.md) is accepted. Cluster 9 is in progress,
not complete or gate-approved. Clusters 0–8 remain approved; the full
scientific plan has 25 clusters, so 9/25 (36%) is a cluster-count proxy, not an effort estimate.
Cross-cutting work already exists without separate cluster gates. This implementation slice does not increase the gate-approved completion percentage.

## Implemented vertical slice

- Strict optional link_down scripts, bounded graph/overlap validation before event registration,
  stable one-based IDs, recovery-before-onset ordering, and a canonical script digest golden.
- Per-run coordinator reusing existing Critical link-state events, exactly-once domain lifecycle,
  empty-service restoration, and pending state for stopped simulations.
- A borrowed synchronous transport down-drop observer, disconnected by coordinator destruction.
  It reports facts without choosing routes or forwarding outcomes. Read-only collective/job owner
  lookups support bounded, unique chunk attribution and affected-job pairs independently of telemetry.
- Legacy runs retain domain digest v1 and output; link-only scripted reports resolve to digest v2 and include
  incident snapshots, lifecycle and drop facts. Use resolved_domain_outcome_digest_version(report)
  when consuming reports; the original constant describes legacy v1 only.
- A checked-in 512-GPU spine-link demo, later admissions using surviving paths, restoration
  eligibility, and analytical tests for same-time cancellation, propagation and fatal retention limits.

The parser accepts link_down, link_degraded, port_down, switch_down and gpu_down, plus bounded
burst/checkpoint loads. Load runs use outcome digest v4; resource/capacity runs retain v3 and prior
link-only runs retain v2.
No-route
job attribution and catalog-v2 telemetry export remain future work. JSON/JSONL CLI export rejects
scripted runs; the underlying catalog-v1 telemetry snapshot contains only existing domain metrics,
not a complete failure report. No full-trace reconstruction claim is made for incident state yet.
The added capacity fields use a separate v3 digest layout; prior v1/v2 encodings remain unchanged.

See [slice evidence](../benchmarks/cluster-9-link-failure-slice.md) and the runnable
[spine-link scenario](../../examples/training/spine-link-failure.yaml). Full Cluster 9 scope and
acceptance requirements below remain open. The resource/capacity
refinement adds typed capacity event kind 9 without increasing the event envelope.

The [resource/capacity slice evidence](../benchmarks/cluster-9-resource-capacity-slice.md) covers
active-rate snapshots, asymmetric restoration, cross-resource overlap, persistent GPU health,
capacity-aware route choices, mode invariance, and preflight overflow rejection. Slice 3 below is
implemented. Slice 4 is also implemented; see the [background load evidence](../benchmarks/cluster-9-background-load-slice.md).
No-route job attribution and final telemetry/comparison/gate evidence remain open.

## Existing boundaries to reuse

| Boundary | Current source | Planned integration |
|---|---|---|
| Typed priorities and dispatch | simulator/include/nexuslab/sim/event.hpp | Small typed failure/load events; explicit new trace kinds |
| Link/port/switch reconciliation | simulator/src/transport/runtime.cpp, failure.cpp | Route every scripted down/up through the existing facade |
| FIFO serializer timing | simulator/src/transport/link_service.cpp, link_queue.cpp | Add effective-capacity updates and active timing snapshots |
| Route candidates and policy views | simulator/src/routing/paths.cpp, router.cpp, policy.cpp | Preserve revision invalidation; expose effective capacity |
| GPU inventory and abort/release | simulator/src/workload/scheduling.cpp | Reuse scheduler-owned persistent health |
| Run composition and outcome pump | simulator/src/workload/run.cpp; include/nexuslab/workload/dispatcher.hpp | Own the coordinator; distinguish collective and load transfers |
| Scenario validation and digest | simulator/src/workload/scenario.cpp, digest.cpp | Validate script before scheduling; explicit digest versions |
| Telemetry observations/catalog | simulator/include/nexuslab/telemetry/records.hpp; src/telemetry/metrics.cpp | Activate precise failure semantics and catalog version 2 |

The simulator/include/nexuslab/failures and simulator/src/failures modules hold the
script model, validation, coordinator and snapshots. Exact class/file decomposition may be refined
without moving domain ownership or changing the ADR contract. This plan adds no dependency.

## Implementation slices

1. **Accept the contract and normalize scripts.** Resolve ADR-019 before runtime changes. Implement
   strict optional schema, preflight bounds/target/overlap checks, canonical action ordering and
   script digest golden vectors. Preserve no-script parsing and digest fixtures. Add the narrow
   calendar/runtime interfaces; do not implement experiment matrices or storage.
2. **Prove the spine-link down/up path.** Add lifecycle events, coordinator/dispatcher integration,
   immediate detection, synchronous transport reconciliation, failure-to-job/chunk attribution and
   queryable incident results independent of telemetry mode. Add a generated-topology fixture that
   verifies its selected physical link is a leaf–spine fabric link. Prove both down and restoration.
3. **Complete resource and capacity semantics.** Add ports/switches, cross-resource overlap, persistent
   scheduler GPU down/up and capacity degradation with start-time timing snapshots. Update routing
   views/revision metadata and analytical timing tests. No automatic retry or job restart.
4. **Add bounded traffic perturbations.** Route burst/checkpoint requests through transport with
   separate ownership, terminal results, backpressure/drop accounting and injection bounds. Prove
   that their completions never reach the collective owner and that they contend with real jobs.
5. **Finish telemetry, comparisons and evidence.** Activate catalog version 2, additive records,
   versioned domain digests and reconstruction. Run the same locked spine failure under ECMP and
   queue-aware, plus a no-failure baseline per policy. Preserve negative/unfinished outcomes. Measure
   overhead and event growth, run the complete Linux validation script, then write Gate 9.

Slices stay within Cluster 9 and are individually reviewable. The anchor scenario is the first
vertical slice, not a deferral of degraded links, switches, GPU failure or traffic perturbations.
Any narrower release requires an explicit master-plan scope revision; do not mark the cluster
complete after only link down/up.

## Anchor comparison

Use a deterministic 512-GPU Clos with fixed workload arrivals, placements, compute, gradient/chunk
sizes, capacities, buffers, seed and a named leaf–spine physical link selected during fixture
construction. At least one transfer must be active or queued on the target at onset. Use a later,
independent job arrival after detection to demonstrate selection of a surviving path, and another
after restoration to prove renewed eligibility. Failing existing jobs remain failed.

Run no-failure and failed/recovered variants separately for each routing policy. Compare matched
script digests and normalized inputs, not raw YAML file equality when policy fields differ. Assert
route validity and invariants; do not assert one policy wins. Record actual selected paths and
transfer/job outcomes. The command will use the existing train --file entry point after its parser
is extended; no compare/matrix command is claimed today.

## Acceptance matrix

| Case | Independent expected proof |
|---|---|
| No script | Existing scenarios, outcome digests and output fixtures unchanged |
| Invalid input | Unknown/duplicate fields, bad IDs/kinds, zero values, same-target overlap, overflow, unsupported delays and bounds rejected before event registration |
| Before traffic, including time zero | Critical state application precedes new admission; no stale route used |
| Active and queued failure | Completion cancelled; whole chunks down-dropped exactly once; occupancy zero; bytes conserved; elapsed busy time closes |
| Same time as completion/arrival | Critical failure precedes Control completion and Normal arrival with golden action/delivery order |
| Propagation | Chunk already serialized survives; next unavailable arc rejects it on arrival |
| Recovery and repeat episodes | Empty idle service can admit new work; no retry/restart; adjacent episodes recover before onset; separated episodes have distinct IDs |
| Link plus port/switch | Clearing a link incident cannot override a remaining down cause; effective health and incident resolution differ explicitly |
| Capacity change | At 8 Gbit/s a 1,000-byte chunk begun at 0 completes serialization at 1,000 ns; degradation to 4 Gbit/s at 500 ns leaves it unchanged; a waiting chunk starts at 1,000 and finishes at 3,000 ns, even if recovery occurs at 1,500 ns; add propagation separately |
| No route | Explicit failed admission/job outcome; no fake dropped bytes; graceful exhaustion with no retry loop |
| Persistent GPU failure | Owner aborts once, all its GPUs released, failed GPU remains ineligible, new admission possible after restoration; legacy job-scoped controls retain meaning |
| Blocked waiter | Queue exhaustion leaves an unfinished result and nonzero CLI status; no invented completion time |
| Background loads | Exact request order/bytes, finite-buffer contention, separate transfer ownership, explicit success/no-route/drop outcomes |
| Attribution | One chunk belongs to at most one incident; one affected-job increment per incident/job pair; overlapping failures do not duplicate logical bytes |
| Detection/recovery metrics | Detection is zero; recovery is onset-to-restoration; no recovery sample for permanent/pending impairment; no confusion with application recovery |
| Determinism | At least three identical runs have equal incident order, script/outcome digests, drop attribution and domain results |
| Telemetry modes | Four modes preserve outcomes; complete full trace reconstructs the catalog-v2 live summary byte-for-byte |
| Version compatibility | Legacy catalog/schema/digest goldens retained; new golden vectors reject unsupported script/catalog/digest versions |
| Limits and exceptions | Explicit bounded failure before misleading complete output; no rollback promise; no silent truncation |

## Gate evidence

Run bash scripts/validate-baseline.sh on the implementation revision: GCC Debug/Release, Clang,
ASan/UBSan, formatting, clang-tidy and registered smoke tests. The 2026-11-22 baseline at
75b45ef validates the development workflow; it is historical workflow evidence, separate from the new slice results.

Add a Release failure benchmark covering no-script versus scripted runs, repeated episodes,
concurrent incidents and the 512-GPU anchor. Report exact revision/toolchain, event and retained
record counts, runtime/RSS, completed/failed/unfinished outcomes, and deterministic digests.
Measure event size against the existing Gate 1 budget; run one-million-event evidence for event
envelope growth and ten-million measurements if queue/pending-state changes are material. Keep
these reference-host measurements separate from noisy CI smoke checks.

Gate 9 must explicitly answer whether timing/detection/recovery, in-flight bytes, unreachable
outcomes and versioned script identity are defined and tested, and whether comparisons hold the
failure inputs fixed. Link runnable scenarios, raw evidence and limitations from the gate report.
No dashboard is required now: complete serializable timelines must support its future consumer.
Delayed detection and two genuine recovery-policy baselines are not claimed by this gate.
