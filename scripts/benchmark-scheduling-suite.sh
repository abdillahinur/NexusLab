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
readonly benchmark="${build_dir}/simulator/nexuslab_scheduling_benchmarks"
for repetition in 1 2 3; do
    echo "repetition=${repetition}"
    for policy in first-fit random rack-local compact; do
        for scenario in sequential burst mixed failure rack-pressure; do
            "${benchmark}" --policy "${policy}" --case "${scenario}" --gpus 128
        done
        for gpus in 64 512 2048 8192; do
            "${benchmark}" --policy "${policy}" --case planning --gpus "${gpus}"
        done
    done
done
