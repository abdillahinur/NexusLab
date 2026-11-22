#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
readonly project_root="$(nexuslab_project_root)"
readonly baseline_root="$(nexuslab_build_root "${project_root}")/baseline"

if [[ "$(uname -s)" != Linux ]]; then
    echo "baseline validation requires Linux; see docs/development-baseline.md for Docker setup" >&2
    exit 2
fi

for tool in cmake ctest ninja gcc g++ clang clang++ clang-format clang-tidy git; do
    command -v "${tool}" >/dev/null || { echo "missing required tool: ${tool}" >&2; exit 2; }
done

mkdir -p "${baseline_root}/logs"
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
cd "${project_root}"

run_check() {
    local name="$1"
    shift
    echo "Running ${name}"
    "$@" 2>&1 | tee "${baseline_root}/logs/${name}.log"
}

run_check environment bash -c '
    set -euo pipefail
    uname -a
    cat /etc/os-release
    for tool in cmake ninja g++ clang++ clang-format clang-tidy; do
        "$tool" --version
    done
'
run_check gcc-debug env CC=gcc CXX=g++ NEXUSLAB_BUILD_ROOT="${baseline_root}/gcc" bash scripts/test.sh dev
run_check gcc-release env CC=gcc CXX=g++ NEXUSLAB_BUILD_ROOT="${baseline_root}/gcc" bash scripts/test.sh release
run_check clang-debug env CC=clang CXX=clang++ NEXUSLAB_BUILD_ROOT="${baseline_root}/clang" bash scripts/test.sh dev
run_check sanitizers env CC=clang CXX=clang++ NEXUSLAB_BUILD_ROOT="${baseline_root}/clang" \
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    bash scripts/test.sh sanitize
run_check formatting bash scripts/format.sh --check
run_check clang-tidy env CC=clang CXX=clang++ NEXUSLAB_BUILD_ROOT="${baseline_root}/clang" bash scripts/lint.sh
run_check clang-tidy-tests ctest --test-dir "${baseline_root}/clang/lint" --output-on-failure

echo "Baseline validation passed. Logs: ${baseline_root}/logs"
