// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/collective/runtime.hpp"
#include "nexuslab/workload/model.hpp"
#include <string_view>
namespace nexuslab::workload {
struct Profile final {
    std::string name;
    std::string assumption;
    std::uint32_t steps;
    sim::SimDurationNs compute;
    transport::ByteCount gradient;
    transport::ByteCount bucket;
};
struct JobControl final {
    std::size_t job;
    WorkloadEventKind kind;
    sim::SimTimeNs timestamp;
    std::uint32_t worker{0};
};
struct TrainingScenario final {
    std::size_t gpus{64};
    std::uint64_t seed{42};
    std::string routing_policy{"ecmp"};
    transport::BitsPerSecond bandwidth{100'000'000'000ULL};
    sim::SimDurationNs propagation{500};
    transport::ByteCount buffer{262'144};
    collective::CollectiveConfiguration local{};
    std::vector<JobSpec> jobs;
    std::vector<JobControl> controls;
};
[[nodiscard]] std::vector<Profile> profiles();
[[nodiscard]] TrainingScenario parse_scenario(std::string_view yaml);
} // namespace nexuslab::workload
