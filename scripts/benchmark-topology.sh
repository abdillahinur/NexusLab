#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

readonly project_root="$(nexuslab_project_root)"
readonly build_root="$(nexuslab_build_root "${project_root}")"
readonly build_dir="${build_root}/release"

declare -a gpu_counts=(64 512 2048 8192)
if (($# > 1)); then
    echo "Usage: scripts/benchmark-topology.sh [GPU_COUNT]" >&2
    exit 2
fi
if (($# == 1)); then
    if [[ ! "$1" =~ ^[1-9][0-9]*$ ]]; then
        echo "GPU count must be a positive integer" >&2
        exit 2
    fi
    gpu_counts=("$1")
fi

cd "${project_root}"
cmake --preset release -B "${build_dir}"
cmake --build "${build_dir}"
for gpu_count in "${gpu_counts[@]}"; do
    "${build_dir}/simulator/nexuslab_topology_benchmarks" --gpus "${gpu_count}"
done
