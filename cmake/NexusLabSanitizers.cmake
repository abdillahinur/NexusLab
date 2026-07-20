# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

function(nexuslab_configure_sanitizers target_name)
    if(NOT NEXUSLAB_ENABLE_SANITIZERS)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "NexusLab sanitizers require GCC or Clang")
    endif()

    target_compile_options(
        "${target_name}"
        INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
    )
    target_link_options("${target_name}" INTERFACE -fsanitize=address,undefined)
endfunction()
