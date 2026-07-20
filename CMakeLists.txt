# SPDX-FileCopyrightText: 2026 NexusLab contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(
    NexusLab
    VERSION 0.1.0
    DESCRIPTION "Deterministic experimentation platform for AI training infrastructure"
    LANGUAGES CXX
)

include(CTest)

option(NEXUSLAB_BUILD_TESTS "Build NexusLab tests" ON)
option(NEXUSLAB_BUILD_BENCHMARKS "Build NexusLab benchmark executables" ON)
option(NEXUSLAB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NEXUSLAB_ENABLE_CLANG_TIDY "Run clang-tidy while compiling NexusLab targets" OFF)
option(NEXUSLAB_WARNINGS_AS_ERRORS "Treat NexusLab compiler warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/EHsc)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(NexusLabWarnings)
include(NexusLabSanitizers)
include(NexusLabClangTidy)

add_library(nexuslab_project_options INTERFACE)
target_compile_features(nexuslab_project_options INTERFACE cxx_std_20)
nexuslab_configure_warnings(nexuslab_project_options)
nexuslab_configure_sanitizers(nexuslab_project_options)

include(NexusLabDependencies)

add_subdirectory(simulator)
