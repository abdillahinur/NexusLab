# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

add_library(
    nexuslab_core STATIC
    src/sim/deterministic_rng.cpp
    src/sim/event_id.cpp
    src/sim/event_queue.cpp
    src/sim/simulation.cpp
    src/version.cpp
)
add_library(NexusLab::core ALIAS nexuslab_core)

target_include_directories(
    nexuslab_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(nexuslab_core PUBLIC nexuslab_project_options)
nexuslab_enable_clang_tidy(nexuslab_core)

add_executable(nexuslab src/main.cpp)
target_link_libraries(nexuslab PRIVATE NexusLab::core nexuslab_project_options)
nexuslab_enable_clang_tidy(nexuslab)

if(NEXUSLAB_BUILD_TESTS AND BUILD_TESTING)
    add_executable(
        nexuslab_tests
        tests/event_queue_test.cpp
        tests/foundation_test.cpp
        tests/simulation_primitives_test.cpp
        tests/simulation_test.cpp
        tests/version_test.cpp
    )
    target_link_libraries(
        nexuslab_tests
        PRIVATE
            NexusLab::core
            GTest::gmock
            GTest::gtest_main
            yaml-cpp::yaml-cpp
            nexuslab_project_options
    )
    nexuslab_enable_clang_tidy(nexuslab_tests)

    include(GoogleTest)
    gtest_discover_tests(nexuslab_tests)

    add_test(NAME nexuslab_cli_version COMMAND nexuslab --version)
    set_tests_properties(nexuslab_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "NexusLab 0.1.0")
endif()

if(NEXUSLAB_BUILD_BENCHMARKS)
    add_executable(nexuslab_benchmarks benchmarks/foundation_benchmark.cpp)
    target_link_libraries(nexuslab_benchmarks PRIVATE NexusLab::core nexuslab_project_options)
    nexuslab_enable_clang_tidy(nexuslab_benchmarks)
endif()
