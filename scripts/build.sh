#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

readonly project_root="$(nexuslab_project_root)"
readonly build_root="$(nexuslab_build_root "${project_root}")"
readonly preset="${1:-dev}"
readonly build_dir="${build_root}/${preset}"

case "${preset}" in
    dev|release|sanitize|lint) ;;
    *)
        echo "unknown build preset: ${preset}" >&2
        echo "expected one of: dev, release, sanitize, lint" >&2
        exit 2
        ;;
esac

cd "${project_root}"
cmake --preset "${preset}" -B "${build_dir}"
cmake --build "${build_dir}"
