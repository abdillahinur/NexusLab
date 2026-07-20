<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Contributing to NexusLab

## Before changing code

1. Read `NEXUSLAB_MASTER_PLAN.md`, `ARCHITECTURE.md`, and the relevant ADRs.
2. Identify the active implementation cluster and its allowed scope.
3. Record architecture or public-interface changes before implementation.
4. Do not add dependencies without approval and an exact version pin.

## Development environment

Linux or WSL2 is the required development target. Install CMake 3.20+, Ninja, a C++20-capable GCC or Clang toolchain, Git, clang-format, and clang-tidy.

On WSL2, repositories mounted from Windows automatically use `~/.cache/nexuslab-build` for generated artifacts to avoid DrvFS permission and performance issues. The `NEXUSLAB_BUILD_ROOT` environment variable overrides this location.

```bash
bash scripts/build.sh dev
bash scripts/test.sh dev
bash scripts/format.sh --check
bash scripts/lint.sh
```

Use `sanitize` instead of `dev` to run with AddressSanitizer and UndefinedBehaviorSanitizer.

## Code requirements

- Preserve deterministic behavior.
- Use integer units for core simulated time once Cluster 1 begins.
- Keep policy implementations out of the simulation core.
- Make error cases explicit; do not silently fall back on invalid inputs.
- Add tests for invariants and edge cases.
- Measure before optimizing and retain before/after evidence.
- Add SPDX headers to project-authored files where the format permits comments.

## Pull requests

Each pull request must name one cluster, explain any interface changes, report exact test and benchmark commands, update documentation, and include the relevant architecture-gate questions. Do not disable failing tests, weaken warnings, or report synthetic benchmark values as real-cluster outcomes.

## Commit guidance

Prefer small commits with imperative subjects. Do not commit generated build directories, downloaded dependencies, local result files, or editor metadata.
