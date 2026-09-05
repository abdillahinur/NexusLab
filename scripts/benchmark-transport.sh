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
"${build_dir}/simulator/nexuslab_transport_benchmarks" "$@"
