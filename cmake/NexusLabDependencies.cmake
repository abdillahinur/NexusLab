# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

include(FetchContent)

# Exact upstream commits corresponding to the approved releases. Keep these
# immutable so a repeated configure resolves the same source revisions.
set(NEXUSLAB_YAML_CPP_REVISION "56e3bb550c91fd7005566f19c079cb7a503223cf") # yaml-cpp-0.9.0
set(NEXUSLAB_GOOGLETEST_REVISION "52eb8108c5bdec04579160ae17225d66034bd723") # v1.17.0

set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG "${NEXUSLAB_YAML_CPP_REVISION}"
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(yaml-cpp)

# NexusLab's warning policy must not promote warnings originating in approved
# third-party headers to project errors.
get_target_property(NEXUSLAB_YAML_CPP_INCLUDE_DIRECTORIES yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
if(NEXUSLAB_YAML_CPP_INCLUDE_DIRECTORIES)
    target_include_directories(
        yaml-cpp
        SYSTEM
        INTERFACE ${NEXUSLAB_YAML_CPP_INCLUDE_DIRECTORIES}
    )
endif()

if(NEXUSLAB_BUILD_TESTS AND BUILD_TESTING)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG "${NEXUSLAB_GOOGLETEST_REVISION}"
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()
