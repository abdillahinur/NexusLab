// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nexuslab/collective/model.hpp"
#include "nexuslab/sim/time.hpp"
#include "nexuslab/transport/types.hpp"
#include "nexuslab/workload/events.hpp"
#include <optional>
#include <string>
#include <vector>
namespace nexuslab::workload {
enum class JobState : std::uint8_t {
    Scheduled = 1,
    Computing = 2,
    Overlapping = 3,
    Communicating = 4,
    Succeeded = 5,
    Failed = 6,
    Cancelled = 7
};
enum class CollectiveKind : std::uint8_t { AllReduce = 1 };
enum class CollectiveAlgorithm : std::uint8_t { Ring = 1 };
struct JobSpec final {
    std::string name;
    std::vector<topology::GpuId> workers;
    std::vector<sim::SimDurationNs> compute;
    sim::SimTimeNs arrival;
    std::uint32_t steps{1};
    transport::ByteCount gradient_bytes{65'536};
    transport::ByteCount bucket_bytes{65'536};
    transport::ByteCount chunk_bytes{4'096};
    std::uint32_t priority{0};
    bool overlap{false};
    CollectiveKind collective{CollectiveKind::AllReduce};
    CollectiveAlgorithm algorithm{CollectiveAlgorithm::Ring};
    bool operator==(const JobSpec&) const = default;
};
struct WorkloadLimits final {
    std::size_t jobs{10'000};
    std::size_t worker_entries{1'000'000};
    std::size_t compute_event_entries{1'000'000};
    std::size_t workers_per_job{8'192};
    std::uint32_t steps_per_job{1'000'000};
    std::uint32_t buckets_per_step{4'096};
    std::size_t compute_events_per_job{1'000'000};
    std::size_t timeline_entries{1'000'000};
};
struct JobSnapshot final {
    JobId id;
    JobState state;
    std::uint32_t completed_steps;
    sim::SimTimeNs arrival;
    std::optional<sim::SimTimeNs> finished;
    std::uint64_t compute_gpu_ns;
    std::uint64_t idle_gpu_ns;
    std::uint64_t elapsed_ns;
    std::string reason;
    bool operator==(const JobSnapshot&) const = default;
};
struct JobTimeline final {
    JobId job;
    sim::SimTimeNs timestamp;
    JobState state;
    std::uint32_t step;
    std::uint32_t bucket;
    std::string action;
    std::optional<collective::CollectiveId> collective;
    bool operator==(const JobTimeline&) const = default;
};
[[nodiscard]] std::uint64_t checked_sum(std::uint64_t left, std::uint64_t right);
[[nodiscard]] std::uint64_t checked_product(std::uint64_t left, std::uint64_t right);
[[nodiscard]] bool terminal(JobState state) noexcept;
} // namespace nexuslab::workload
