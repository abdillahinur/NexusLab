<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Development baseline

Linux or WSL2 remains the supported development target. On macOS, use Docker Desktop to run
Linux checks; native Apple Clang builds are not a substitute for this baseline.

## Complete validation

From the repository root on Linux, with the tools listed in `CONTRIBUTING.md` installed:

```bash
bash scripts/validate-baseline.sh
```

The fail-fast script runs GCC Debug and Release, Clang Debug, Clang ASan/UBSan with leak
detection, formatting, and a clang-tidy build followed by its tests. All builds retain the
warnings-as-errors policy and the repository's exact yaml-cpp and GoogleTest commit pins.
CTest includes the registered CLI and benchmark smoke checks; this is not a performance study.

Builds and logs live under `build/baseline` by default. Set `NEXUSLAB_BUILD_ROOT` to put the
`baseline` directory elsewhere. Compiler families have separate build trees. The default build
parallelism is four; override `CMAKE_BUILD_PARALLEL_LEVEL` to suit the host. A failed command
returns a nonzero status and leaves its log under `baseline/logs`. Logs are overwritten on rerun;
copy them elsewhere if retaining evidence from multiple revisions.

## Linux container from macOS

Start Docker Desktop, then run these commands from the repository root:

```bash
docker build -f scripts/Dockerfile.baseline -t nexuslab-baseline:local .
docker run --rm \
  --mount "type=bind,src=$PWD,dst=/src,readonly" \
  --mount type=volume,src=nexuslab-baseline-build,dst=/build \
  nexuslab-baseline:local
```

The source mount is read-only. A named Linux volume retains dependency downloads, build trees,
and logs without writing generated artifacts into the checkout. A clean run uses a fresh volume
name. Export logs after a run with:

```bash
docker run --rm \
  --mount type=volume,src=nexuslab-baseline-build,dst=/build,readonly \
  nexuslab-baseline:local tar -C /build/baseline -czf - logs > baseline-logs.tar.gz
```

Keep the exported archive outside version control. The Ubuntu base image is pinned by digest;
APT resolves current Ubuntu packages when building the toolchain, so the Dockerfile is a
reconstructible setup rather than a bit-for-bit frozen toolchain. Record the resulting image ID
and tool versions for a baseline. The first build needs network access to Ubuntu and GitHub.

## Baseline record — 2026-11-22

Source revision: `f891c53cd1d0e2cadc7ad88a686309fa2d31f57c`, with only the baseline tooling and
documentation changes in this commit applied. No simulator source, dependency pin, or scenario
changed. These are fresh local results, separate from historical Gate 8 evidence.

Environment: Docker Desktop on Apple Silicon, Linux ARM64 kernel `6.10.14-linuxkit`, Ubuntu
24.04.4, CMake 3.28.3, Ninja 1.11.1, GCC 13.3.0, and Clang/clang-format/clang-tidy 18.1.3.
Four build workers were used. The existing local image was `nexuslab-toolchain:milestone4`, ID
`sha256:0352d81cce61147cfab6b6c04b6f2a8bf445874e5f09996605d3c12f3e35e6c2`.

| Check | Result |
|---|---|
| GCC Debug, warnings as errors | 276/276 CTest checks passed |
| GCC Release, warnings as errors | 276/276 CTest checks passed |
| Clang Debug, warnings as errors | 276/276 CTest checks passed |
| Clang ASan/UBSan, leak detection enabled | 276/276 CTest checks passed |
| clang-format 18 | Passed |
| clang-tidy 18 and its CTest suite | Build passed; 276/276 CTest checks passed |
| Shell syntax, macOS rejection guard, and whitespace | Passed |

All build trees were created fresh in Docker volume `nexuslab-baseline-20261122`, under
`/build/baseline`. The source checkout was mounted read-only at `/src`. To avoid slow full-history
downloads, clean cached Git repositories in the existing image were used as local mirrors after
verifying their HEADs and unmodified worktrees:

- yaml-cpp: `56e3bb550c91fd7005566f19c079cb7a503223cf`
- GoogleTest: `52eb8108c5bdec04579160ae17225d66034bd723`

The container-local cache configuration and validation command were:

```bash
git config --global url.file:///tmp/build-gcc/_deps/yaml-cpp-src.insteadOf \
  https://github.com/jbeder/yaml-cpp.git
git config --global url.file:///tmp/build-gcc/_deps/googletest-src.insteadOf \
  https://github.com/google/googletest.git
NEXUSLAB_BUILD_ROOT=/build CMAKE_BUILD_PARALLEL_LEVEL=4 bash scripts/validate-baseline.sh
```

This reuses dependency sources, not old object files or binaries. The normal workflow fetches the
same pinned commits directly from GitHub and does not require these mirrors. No host Git
configuration was changed.

The checked-in Dockerfile provides a fresh toolchain recipe. Its APT rebuild was stopped during
slow network downloads; it was not the image used for these results and is not claimed as a
verified fresh image build. Hosted CI was not rerun, and no new performance or Cluster 9 behavior
is claimed by this baseline.
