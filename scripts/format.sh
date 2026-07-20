#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly mode="${1:---write}"

mapfile -d '' sources < <(
    find "${project_root}/simulator" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0
)

if [[ "${#sources[@]}" -eq 0 ]]; then
    echo "no C++ sources found" >&2
    exit 1
fi

case "${mode}" in
    --check)
        clang-format --dry-run --Werror "${sources[@]}"
        ;;
    --write)
        clang-format -i "${sources[@]}"
        ;;
    *)
        echo "usage: $0 [--check | --write]" >&2
        exit 2
        ;;
esac
