<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 9 resource/capacity slice evidence

Date: 2026-11-22. Source: parent 441e07e plus this change. This is incremental implementation
evidence, not Gate 9 approval. No dependency pin, warning policy or suppression was changed.

Environment: Docker Desktop/Linux ARM64, Ubuntu 24.04.4, GCC 13.3.0, Clang/clang-format/clang-tidy
18.1.3, CMake 3.28.3 and Ninja 1.11.1. Existing image nexuslab-toolchain:milestone4,
sha256:0352d81cce61147cfab6b6c04b6f2a8bf445874e5f09996605d3c12f3e35e6c2; volume
nexuslab-baseline-20261122, four build workers. Source is mounted read-only and pinned dependency
sources are reused. Hosted CI was not run here.

## Analytical and domain checks

- At 8 Gbit/s, a 1,000-byte chunk starts at 0 and finishes serialization at 1,000 ns despite a
  4-Gbit/s impairment at 500 ns. A queued chunk starts at 1,000 and finishes at 3,000, despite
  restoration at 1,500. The final 500-byte chunk ends at 3,500; with 25-ns propagation the transfer
  completes at 3,525 ns. Opposite-direction 16-Gbit/s baseline is restored independently.
- Capacity revisions progress 0 → 1 → 2 while topology operational_revision stays unchanged.
  Queue-aware decisions record the current revision and select another path when appropriate,
  without invalidating reachability candidates. Event size remains 72 bytes, payload 32 bytes.
- Overlapping port, switch and link incidents recover independently. Link restoration under a
  failed switch remains unavailable; clearing a port under the switch also remains unavailable.
  Down-dropped chunks are attributed once, and later arrivals choose the lowest active cause ID.
- GPU failure aborts its owner at 2,500 ns; issued traffic drains without failure-attributed network
  drops. A pinned waiter arriving at 3,000 remains blocked until restoration at 10,000 (7,000-ns
  scheduling wait). Time-zero permanent GPU failure leaves a blocked job explicitly unfinished.
- Minimum scripted capacities participate in admission preflight; unrepresentable serialization
  fails before transfer IDs are consumed. Invalid capacities, typed targets, conflicting GPU
  controls and same-link down/degraded overlap are rejected before event registration.
- Resource/GPU/degradation examples repeat identically across all four telemetry modes. New kinds
  use domain digest v3; old v1/v2 encodings and the independent link-only script golden remain.

## Validation and runnable evidence

Command: NEXUSLAB_BUILD_ROOT=/build CMAKE_BUILD_PARALLEL_LEVEL=4 bash scripts/validate-baseline.sh.
Final result: baseline validation passed. All 295/295 CTest checks passed under GCC Debug,
GCC Release, Clang Debug, Clang ASan/UBSan with leak detection, and the clang-tidy build.
Formatting and git diff --check passed. The 19 focused failure tests include nine additions to
the prior link-only slice. No warning suppression or policy relaxation was introduced.

Examples: resource-recovery.yaml and gpu-recovery.yaml intentionally exit 1 because their first
jobs fail. link-degradation.yaml exits 0 when all jobs succeed. Use the existing train --file
entry point with --timeline; complete failure telemetry JSON export remains explicitly rejected.

Executed Release examples (all domain digest version 3):

| Scenario | Exit | Events | Final time ns | Observed result | Script digest |
|---|---:|---:|---:|---|---|
| resource-recovery | 1 | 58 | 21,100 | First job fails at 3,000; later job succeeds at 21,100; port/switch each drop 2,000 distinct bytes | 270f230c5e96348e |
| gpu-recovery | 1 | 68 | 17,100 | Owner fails at 2,500; pinned job waits 7,000 ns then succeeds; zero network drops | 84adfc5cc396e32e |
| link-degradation | 0 | 152 | 16,200 | Both jobs succeed (13,200 and 16,200); zero drops; capacity revision 2 | 7c5941ecc49d294d |

Commands used /build/baseline/gcc/release/simulator/nexuslab train --file
examples/training/<scenario>.yaml --timeline. Failed jobs remain failed; restoration does not
restart them. One job may count under two distinct incidents, but each dropped chunk's logical
bytes are attributed to only one incident. These are executed synthetic examples, not policy
performance comparisons or hardware measurements.

## One-million-event control

The Release nexuslab_benchmarks --events 1000000 control dispatched 1,000,000 events at final
simulated time 999,999 ns, checksum 7106713294135827890. Event/payload sizes are 72/32 bytes.
Insertion: 74,418,208 ns (13,437,571.62 events/s); dispatch: 147,171,125 ns (6,794,811.14 events/s).
Peak RSS: 117,160 KiB; insertion RSS delta: 114,248 KiB. This is one local control during validation,
not a reference-host regression comparison. The kernel queue/pending-state algorithms are unchanged.

## Remaining Cluster 9 work

Background burst/checkpoint loads, no-route incident/job attribution, catalog-v2 emission and full
reconstruction/export, complete comparison/overhead/scale evidence, and Gate 9 remain pending.
The unchanged event envelope does not establish full failure-platform scale or hardware realism.
