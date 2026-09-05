#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"
readonly project_root="$(nexuslab_project_root)"
readonly build_dir="$(nexuslab_build_root "${project_root}")/release"
cd "${project_root}"
cmake --preset release -B "${build_dir}"
cmake --build "${build_dir}"
readonly benchmark="${build_dir}/simulator/nexuslab_routing_benchmarks"
for repetition in 1 2 3; do
    echo "repetition=${repetition}"
    for policy in shortest-path ecmp least-loaded queue-aware; do
        for gpus in 64 512 2048 8192; do
            "${benchmark}" --mode lookup --policy "${policy}" --gpus "${gpus}" --flows 10000
        done
        for pattern in incast all-to-all; do
            "${benchmark}" --policy "${policy}" --pattern "${pattern}" --flows 1000
        done
        "${benchmark}" --policy "${policy}" --gpus 2048 --flows 1000
        "${benchmark}" --policy "${policy}" --flows 10000
    done
done
