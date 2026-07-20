<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Architecture Gate — Cluster 0

Date: 2026-07-19

## Correctness

- The initial repository master plan body was compared character-for-character with the desktop source after normalizing line endings and contained no truncation markers. Subsequent edits are limited to the approved decision resolution and current Cluster 0 status.
- GoogleTest/GoogleMock 1.17.0 and yaml-cpp 0.9.0 are pinned to exact upstream commits.
- Ubuntu 24.04.4 LTS under WSL2 successfully configures, builds, and tests the complete dependency and target graph with both GCC 13.3.0 and Clang 18.1.3.
- All four CTest cases pass with both compilers, covering GoogleTest, GoogleMock, yaml-cpp, the version API, and the CLI version command.
- The sanitizer preset passes all tests under AddressSanitizer and UndefinedBehaviorSanitizer with Clang 18.1.3.

## Abstraction

- Cluster 0 introduces only a version API, CLI shell, test harness, and benchmark harness.
- No simulation, topology, transfer, routing, workload, or replay interfaces were invented ahead of their owning clusters.
- Project warning and sanitizer policies are carried by an interface target and do not impose NexusLab warning flags on fetched dependencies.

## Performance

- The release benchmark executable ran 100,000 deterministic harness iterations in 84,053 ns and emitted checksum `5134964229064569837` on this machine.
- Its output explicitly states that it measures the harness only, not simulation performance.
- Event throughput and regression thresholds are intentionally deferred to the measured Cluster 1 baseline.

## Extensibility

- CMake options independently control tests, benchmarks, sanitizers, clang-tidy, and warnings-as-errors.
- Configure presets cover development, release, sanitizer, and lint workflows.
- Fetched dependencies are commit-pinned, and repeat builds use disconnected update mode after the initial download.

## Failure Behavior

- Developer scripts reject unknown presets and invalid benchmark iteration counts.
- CLI and benchmark executables catch exceptions at the process boundary and return explicit nonzero statuses.
- CI jobs are isolated across GCC, Clang, sanitizers, quality checks, and the benchmark smoke test.

## Documentation

- The master plan, architecture, roadmap, contribution guide, changelog, pull-request template, issue templates, and ADR template are present.
- ADR-001 through ADR-003 record discrete-event simulation, integer nanoseconds, and chunk-level transfers.
- Apache-2.0 licensing and SPDX notices are present; no `NOTICE` file is required yet.

## Evidence Recorded

- License SHA-256: `CFC7749B96F63BD31C3C42B5C471BF756814053E847C10F3EB003417BC523D30`.
- Environment: Ubuntu 24.04.4 LTS on WSL2, Linux kernel 6.18.33.2-microsoft-standard-WSL2.
- Toolchain: CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, Clang/clang-format/clang-tidy 18.1.3, Git 2.43.0.
- GCC clean configure and build: passed with warnings treated as errors.
- Clang clean configure and build: passed with warnings treated as errors.
- GCC CTest run: 4/4 tests passed.
- Clang CTest run: 4/4 tests passed.
- ASan/UBSan CTest run with Clang: 4/4 tests passed.
- Linux clang-format dry run: passed.
- Linux clang-tidy build: passed.
- Release benchmark smoke run: 100,000 iterations, 84,053 ns, checksum `5134964229064569837`; harness only, not a Cluster 1 performance baseline.
- WSL-mounted source build handling: passed using the automatic Linux-native cache at `~/.cache/nexuslab-build`.
- GitHub Actions: configured but not run because no commit was pushed.

## Decision

Proceed: **NO**

Required changes:

- Run the configured CI workflow and record its result.
- Update this gate to `Proceed: YES` only after the evidence above is captured.

Cluster 1 must not begin while this gate remains `Proceed: NO`.
