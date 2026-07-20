#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

nexuslab_project_root() {
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd
}

nexuslab_build_root() {
    local source_root="$1"

    if [[ -n "${NEXUSLAB_BUILD_ROOT:-}" ]]; then
        printf '%s\n' "${NEXUSLAB_BUILD_ROOT}"
        return
    fi

    if [[ "${source_root}" == /mnt/* ]] && grep -qi microsoft /proc/sys/kernel/osrelease; then
        printf '%s\n' "${XDG_CACHE_HOME:-${HOME}/.cache}/nexuslab-build"
        return
    fi

    printf '%s\n' "${source_root}/build"
}
