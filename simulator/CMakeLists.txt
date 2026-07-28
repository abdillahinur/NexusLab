# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

add_library(
    nexuslab_core STATIC
    src/sim/deterministic_rng.cpp
    src/sim/event_id.cpp
    src/sim/event_queue.cpp
    src/sim/simulation.cpp
    src/sim/trace.cpp
    src/transport/link_queue.cpp
    src/transport/link_service.cpp
    src/transport/runtime.cpp
    src/transport/timing.cpp
    src/topology/clos.cpp
    src/topology/entities.cpp
    src/topology/export.cpp
    src/topology/families.cpp
    src/topology/graph.cpp
    src/topology/import.cpp
    src/topology/summary.cpp
    src/topology/validation.cpp
    src/version.cpp
)
add_library(NexusLab::core ALIAS nexuslab_core)

target_include_directories(
    nexuslab_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(nexuslab_core PUBLIC nexuslab_project_options PRIVATE yaml-cpp::yaml-cpp)
nexuslab_enable_clang_tidy(nexuslab_core)

add_library(nexuslab_cli_support STATIC src/cli/application.cpp)
target_link_libraries(nexuslab_cli_support PUBLIC NexusLab::core PRIVATE nexuslab_project_options)
nexuslab_enable_clang_tidy(nexuslab_cli_support)

add_executable(nexuslab src/main.cpp)
target_link_libraries(nexuslab PRIVATE nexuslab_cli_support nexuslab_project_options)
nexuslab_enable_clang_tidy(nexuslab)

if(NEXUSLAB_BUILD_TESTS AND BUILD_TESTING)
    add_executable(
        nexuslab_tests
        tests/event_queue_test.cpp
        tests/foundation_test.cpp
        tests/link_queue_test.cpp
        tests/simulation_primitives_test.cpp
        tests/simulation_test.cpp
        tests/trace_test.cpp
        tests/transport_event_test.cpp
        tests/transport_primitives_test.cpp
        tests/transport_runtime_test.cpp
        tests/topology_clos_test.cpp
        tests/topology_entities_test.cpp
        tests/topology_export_test.cpp
        tests/topology_families_test.cpp
        tests/topology_graph_test.cpp
        tests/topology_import_test.cpp
        tests/topology_operational_test.cpp
        tests/topology_summary_test.cpp
        tests/topology_validation_test.cpp
        tests/version_test.cpp
    )
    target_link_libraries(
        nexuslab_tests
        PRIVATE
            NexusLab::core
            nexuslab_cli_support
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
    add_test(NAME nexuslab_cli_initial_topology COMMAND nexuslab topology summary --clos initial)
    set_tests_properties(
        nexuslab_cli_initial_topology
        PROPERTIES PASS_REGULAR_EXPRESSION "source=clos:initial[\r\n]+racks=8[\r\n]+gpus=512"
    )
    add_test(
        NAME nexuslab_cli_topology_file
        COMMAND
            nexuslab topology summary --file
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/direct-topology.yaml"
    )
    set_tests_properties(
        nexuslab_cli_topology_file
        PROPERTIES PASS_REGULAR_EXPRESSION "source=file:.*direct-topology.yaml[\r\n]+racks=1[\r\n]+gpus=1"
    )
endif()

if(NEXUSLAB_BUILD_BENCHMARKS)
    add_executable(nexuslab_benchmarks benchmarks/simulation_benchmark.cpp)
    target_link_libraries(nexuslab_benchmarks PRIVATE NexusLab::core nexuslab_project_options)
    nexuslab_enable_clang_tidy(nexuslab_benchmarks)

    add_executable(nexuslab_topology_benchmarks benchmarks/topology_benchmark.cpp)
    target_link_libraries(
        nexuslab_topology_benchmarks
        PRIVATE NexusLab::core nexuslab_project_options
    )
    nexuslab_enable_clang_tidy(nexuslab_topology_benchmarks)

    if(NEXUSLAB_BUILD_TESTS AND BUILD_TESTING)
        add_test(NAME nexuslab_benchmark_smoke COMMAND nexuslab_benchmarks --events 1000)
        set_tests_properties(
            nexuslab_benchmark_smoke
            PROPERTIES PASS_REGULAR_EXPRESSION "benchmark=simulation_core"
        )
        add_test(
            NAME nexuslab_topology_benchmark_smoke
            COMMAND nexuslab_topology_benchmarks --gpus 64
        )
        set_tests_properties(
            nexuslab_topology_benchmark_smoke
            PROPERTIES PASS_REGULAR_EXPRESSION "benchmark=topology"
        )
    endif()
endif()
