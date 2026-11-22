<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Cluster 9 bounded background load slice evidence

Date: 2026-11-22. Source: parent d2dead4 plus this change. This records slice 4, not Gate 9
approval. Clusters 0–8 remain gate-approved; no-route job attribution, catalog-v2 failure/load
telemetry, full reconstruction, controlled policy comparisons and final Gate 9 evidence remain open.

Environment: Docker Desktop/Linux ARM64, Ubuntu 24.04.4, GCC 13.3.0, Clang/clang-format/clang-tidy
18.1.3, CMake 3.28.3, Ninja 1.11.1. Existing image nexuslab-toolchain:milestone4,
sha256:0352d81cce61147cfab6b6c04b6f2a8bf445874e5f09996605d3c12f3e35e6c2. Source mounted read-only;
volume nexuslab-baseline-20261122, four workers, unchanged pinned dependency sources. Hosted CI
was not run. No dependency, warning policy or suppression was changed.

## Contract and analytical checks

- Sources are bounded at 10,000, expanded requests at 100,000. Unknown kinds/fields, missing fields,
  nonexistent/shared-NIC endpoints, nonpositive counts/sizes, time overflow, per-source and aggregate
  byte overflow are rejected before event registration. Existing transport/telemetry limits still
  fail runs explicitly; no memory exhaustion rollback is promised.
- At 8 Gbit/s and 25-ns propagation, 1,000-byte direct-link requests injected at 0 and 5,000 ns
  finish at 1,025 and 6,025 ns, each delivering exactly 1,000 bytes.
- Simultaneous zero-interval requests use source/ordinal order. With a 1,000-byte waiting buffer,
  the active and first waiting request succeed; the third request drops its 1,000 bytes.
- A direct-link incident at 500 ns drops the first active 1,000-byte request. Admission at 1,000
  yields NoRoute without transfer ID or fabricated dropped bytes. Critical recovery at 2,000
  precedes the same-time injection, which succeeds. The incident receives 1,000 distinct dropped
  bytes and zero fake affected jobs.
- Stopping at 500 ns retains one injected pending transfer and one uninjected pending request,
  with no invented completion times. Dedicated ownership prevents background completions from
  reaching the ring executor. Lower-half collective flow keys and upper-half load keys cannot
  collide; legacy allocations keep their previous keys.
- Bootstrap timing preflight uses independent path lookup, reserved minimum capacities and the
  last injection time without consuming routing decisions or RNG draws. Admission checks timing
  again because outages/queues can change the actual path and start times.
- Repeat runs across off/summary/sampled/full preserve request records and domain digests. Load
  scripts use explicit outcome digest v4. Empty-load/legacy v1, link-only v2, resource/capacity v3
  layouts and earlier golden vectors remain unchanged. Catalog-v1 telemetry is still incomplete
  for scripted lifecycle state; CLI JSON/JSONL export remains rejected.

The independent script golden uses generate_two_gpu_direct(), no incidents, one burst source
(GPU 0 to 1, onset 0, count 2, interval 5,000, bytes 1,000, chunk 1,000). Starting with the
FNV-1a offset 14695981039346656037, encode the domain, version, canonical topology and detector
as ADR-019 specifies, incident count 0 and load count 1, followed by little-endian u64 values
[1, 1, 0, 1, 0, 2, 5000, 1000, 1000]. Independent Python struct.pack('<Q', value) calculation
produced 0fda0f6882b57b1a (1142242397035789082), matching the C++ test. Changing load provenance
or schedule changes the checksum. Script identity alone does not identify a whole experiment.

## Executed Release examples

Command: /build/baseline/gcc/release/simulator/nexuslab train --file
examples/training/<name>.yaml --timeline. The baseline removes failure_script from
background-loads.yaml; its topology, job, routing, seed and transport settings remain fixed.
The loss example deliberately changes the job to local training and the buffer to one byte;
it demonstrates explicit negative outcomes, not a controlled contention comparison.

| Scenario | Exit | Events | Final time ns | Training elapsed ns | Load delivered / buffer-dropped bytes |
|---|---:|---:|---:|---:|---:|
| Matching no-load baseline | 0 | 43 | 7,100 | 7,100 | 0 / 0 |
| background-loads | 0 | 147 | 23,100 | 23,100 | 20,000 / 0 |
| background-load-loss | 1 | 38 | 12,550 | 1,000 | 3,000 / 17,000 |

Both scripted examples resolve to digest v4 and script digest 5b6e54c88c847d41. Equal script
identity is expected despite the intentionally different buffers/workloads. Their sources are
reported separately from incidents. The successful example's first declared burst is injected at
1,000 ns after the second declared checkpoint source's first request at 500 ns; transfer IDs and
report schedules retain this actual ordering. The loss example keeps its local job succeeded and
exits 1 because all four load requests fail. Requested bytes equal delivered plus reason-specific
drops for every admitted terminal request. No retry, storage checkpoint consistency, application
restart or routing-policy superiority is claimed.

## Validation

Command: NEXUSLAB_BUILD_ROOT=/build CMAKE_BUILD_PARALLEL_LEVEL=4 bash scripts/validate-baseline.sh.
Final result: baseline validation passed. All 308/308 checks pass under GCC Debug, GCC Release,
Clang Debug, ASan/UBSan with leak detection, and the clang-tidy build. Twelve focused TrafficTest
checks and a checked-in example CLI smoke check extend the prior 295 checks to 308. Formatting
and git diff --check pass.
Raw logs and example/control output are retained outside the source checkout in the task workspace.

## One-million-event control

Command: /build/baseline/gcc/release/simulator/nexuslab_benchmarks --events 1000000.
Trace disabled, /proc/self/status memory source. Event size 72 bytes; payload size 32 bytes.
Insertion 74,138,500 ns (13,488,268.58 events/s); dispatch 141,915,000 ns (7,046,471.48 events/s).
RSS before 2,912 KiB, after insertion/dispatch 117,164 KiB, insertion delta 114,252 KiB;
peak RSS 117,164 KiB. Dispatched 1,000,000 events; final simulated time 999,999;
checksum 7106713294135827890. New typed injection kind 10 is appended without renumbering old
variants/kinds or growing the event envelope. This single control ran alongside validation;
wall-clock throughput and RSS are observational, not a performance-regression acceptance claim.
