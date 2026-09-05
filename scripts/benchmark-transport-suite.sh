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
readonly benchmark="${build_dir}/simulator/nexuslab_transport_benchmarks"
for repetition in 1 2 3; do
    echo "repetition=${repetition}"
    "${benchmark}" --pattern pipeline --flows 1 --bytes 333333 --chunk-bytes 1 --buffer-bytes 333333
    for pattern in incast all-to-all; do
        for flows in 100 10000; do
            "${benchmark}" --pattern "${pattern}" --flows "${flows}"
        done
    done
    for pattern in incast all-to-all; do
        "${benchmark}" --pattern "${pattern}" --flows 10000 --buffer-bytes 1073741824
    done
    "${benchmark}" --pattern all-to-all --flows 4032
    for chunk_bytes in 1024 4096 16384; do
        "${benchmark}" --pattern incast --flows 100 --chunk-bytes "${chunk_bytes}"
    done
    "${benchmark}" --pattern incast --flows 100 --gpus 2048
done
