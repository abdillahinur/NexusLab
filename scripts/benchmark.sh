#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

readonly project_root="$(nexuslab_project_root)"
readonly build_root="$(nexuslab_build_root "${project_root}")"
readonly build_dir="${build_root}/release"
readonly event_count="${1:-1000000}"

if [[ ! "${event_count}" =~ ^[1-9][0-9]*$ ]]; then
    echo "event count must be a positive integer" >&2
    exit 2
fi

cd "${project_root}"
cmake --preset release -B "${build_dir}"
cmake --build "${build_dir}"
"${build_dir}/simulator/nexuslab_benchmarks" --events "${event_count}"
