# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

function(nexuslab_configure_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            "${target_name}"
            INTERFACE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Wnon-virtual-dtor
                -Wold-style-cast
        )

        if(NEXUSLAB_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" INTERFACE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options("${target_name}" INTERFACE /W4 /permissive-)

        if(NEXUSLAB_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" INTERFACE /WX)
        endif()
    else()
        message(WARNING "NexusLab warning policy is not defined for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()
