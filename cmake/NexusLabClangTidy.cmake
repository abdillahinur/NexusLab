# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

function(nexuslab_enable_clang_tidy target_name)
    if(NOT NEXUSLAB_ENABLE_CLANG_TIDY)
        return()
    endif()

    find_program(NEXUSLAB_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set_property(
        TARGET "${target_name}"
        PROPERTY CXX_CLANG_TIDY
            "${NEXUSLAB_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
    )
endfunction()
