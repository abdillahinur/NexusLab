// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/topology/id.hpp"
#include <cstdint>
namespace nexuslab::workload {
struct JobIdTag final {};
using JobId = topology::StrongId<JobIdTag>;
enum class WorkloadEventKind : std::uint8_t {
    Arrival = 1,
    ComputeReady = 2,
    Cancel = 3,
    WorkerFailure = 4,
    GpuDown = 5,
    GpuUp = 6
};
struct WorkloadEvent final {
    JobId job;
    WorkloadEventKind kind;
    std::uint32_t worker{0};
    std::uint32_t step{0};
    std::uint32_t bucket{0};
    bool operator==(const WorkloadEvent&) const = default;
};
} // namespace nexuslab::workload
