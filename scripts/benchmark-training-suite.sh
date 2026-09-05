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
readonly benchmark="${build_dir}/simulator/nexuslab_training_benchmarks"
for repetition in 1 2 3; do
    echo "repetition=${repetition}"
    for workers in 1 2 4 8 32 128; do
        "${benchmark}" --workers "${workers}" --steps 1
    done
    for workers in 2 8 64 512 8192; do
        "${benchmark}" --mode planning --workers "${workers}"
    done
    for overlap in 0 1; do
        "${benchmark}" --workers 8 --bucket-bytes 16384 --compute-ns 100000 --overlap "${overlap}"
    done
    "${benchmark}" --workers 8 --jobs 8
    "${benchmark}" --workers 8 --jobs 8 --shared-nics 1
    "${benchmark}" --workers 8 --buffer-bytes 0
    for chunk in 1024 16384; do
        "${benchmark}" --workers 8 --chunk-bytes "${chunk}"
    done
done
