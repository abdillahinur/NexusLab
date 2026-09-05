// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/scheduling/policy.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/workload/dispatcher.hpp"
#include "nexuslab/workload/run.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>
#include <source_location>
#include <sstream>
namespace nexuslab::scheduling {
namespace {
using workload::JobId;
using workload::JobState;
template <typename A, typename B>
void equal(const A& a, const B& b,
           std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_EQ(a, b);
}
template <typename Exception, typename Operation> bool throws(Operation operation) {
    try {
        operation();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}
std::vector<topology::GpuId> ids(std::size_t first, std::size_t last) {
    std::vector<topology::GpuId> result;
    for (auto i = first; i < last; ++i) {
        result.emplace_back(i);
    }
    return result;
}
workload::JobSpec job(std::uint32_t workers = 1, sim::SimTimeNs arrival = {}) {
    workload::JobSpec spec;
    spec.name = "scheduler-test";
    spec.requested_workers = workers;
    spec.compute.assign(workers, sim::SimDurationNs{1000});
    spec.arrival = arrival;
    spec.gradient_bytes = transport::ByteCount{64};
    spec.bucket_bytes = transport::ByteCount{64};
    spec.chunk_bytes = transport::ByteCount{64};
    return spec;
}
workload::JobSpec pinned_job(std::uint64_t gpu = 0, sim::SimTimeNs arrival = {}) {
    auto spec = job(1, arrival);
    spec.requested_workers = 0;
    spec.workers.emplace_back(gpu);
    return spec;
}
workload::TrainingScenario scenario() {
    workload::TrainingScenario result;
    result.scheduling = Configuration{};
    return result;
}
Configuration& configuration(workload::TrainingScenario& input) {
    if (!input.scheduling.has_value()) {
        throw std::logic_error{"missing scheduling configuration"};
    }
    return *input.scheduling;
}
const workload::JobSnapshot& snapshot(const workload::TrainingReport& report, std::uint64_t id) {
    const auto found = std::find_if(report.jobs.begin(), report.jobs.end(),
                                    [&](const auto& item) { return item.id == JobId{id}; });
    if (found == report.jobs.end()) {
        throw std::logic_error{"missing test job"};
    }
    return *found;
}
TEST(SchedulingTest, InventoryAllocationIsAtomicAndReleasePreservesFailures) {
    auto graph = topology::generate_clos({64, 8, 8, 8});
    ResourceInventory inventory{*graph};
    inventory.allocate(JobId{0}, ids(0, 2));
    const auto before =
        std::vector<Resource>{inventory.view().gpus.begin(), inventory.view().gpus.end()};
    equal(throws<std::invalid_argument>([&] { inventory.allocate(JobId{1}, ids(1, 4)); }), true);
    equal(std::vector<Resource>(inventory.view().gpus.begin(), inventory.view().gpus.end()),
          before);
    equal(inventory.set_health(topology::GpuId{0}, false), std::optional{JobId{0}});
    inventory.release(JobId{0});
    equal(inventory.view().gpus[0].healthy, false);
    equal(inventory.view().gpus[0].owner.has_value(), false);
    equal(throws<std::invalid_argument>([&] { inventory.allocate(JobId{1}, ids(0, 1)); }), true);
    static_cast<void>(inventory.set_health(topology::GpuId{0}, true));
    inventory.allocate(JobId{1}, ids(0, 1));
}
TEST(SchedulingTest, InventoryRejectsDuplicateUnknownAndRepeatedOwners) {
    auto graph = topology::generate_clos({64, 8, 8, 8});
    ResourceInventory inventory{*graph};
    const std::vector duplicate{topology::GpuId{0}, topology::GpuId{0}};
    equal(throws<std::invalid_argument>([&] { inventory.allocate(JobId{0}, duplicate); }), true);
    equal(throws<std::invalid_argument>([&] { inventory.allocate(JobId{0}, ids(63, 65)); }), true);
    inventory.allocate(JobId{0}, ids(0, 1));
    equal(throws<std::invalid_argument>([&] { inventory.allocate(JobId{0}, ids(1, 2)); }), true);
    equal(throws<std::invalid_argument>(
              [&] { static_cast<void>(inventory.set_health(topology::GpuId{64}, false)); }),
          true);
}
TEST(SchedulingTest, ExactFitTemporaryShortageAndImpossibleRequestsDiffer) {
    auto graph = topology::generate_clos({64, 8, 8, 8});
    ResourceInventory inventory{*graph};
    auto policy = make_policy("first-fit", 42);
    equal(policy->place({JobId{0}, 64, 0, {}}, inventory.view()).workers, ids(0, 64));
    inventory.allocate(JobId{1}, ids(0, 1));
    equal(policy->place({JobId{0}, 64, 0, {}}, inventory.view()).outcome,
          PlacementOutcome::Waiting);
    equal(policy->place({JobId{0}, 65, 0, {}}, inventory.view()).outcome,
          PlacementOutcome::Rejected);
    equal(policy->place({JobId{0}, 0, 0, {}}, inventory.view()).outcome,
          PlacementOutcome::Rejected);
}
TEST(SchedulingTest, RackPreferenceAndCompactAvoidFragmentedRack) {
    auto graph = topology::generate_clos({128, 8, 8, 8});
    ResourceInventory inventory{*graph};
    inventory.allocate(JobId{1}, ids(0, 60));
    const JobRequest request{JobId{0}, 8, 0, {}};
    const auto first = make_policy("first-fit", 42)->place(request, inventory.view());
    equal(locality(inventory.view(), first.workers).racks, 2U);
    for (const auto* name : {"rack-local", "compact"}) {
        const auto placed = make_policy(name, 42)->place(request, inventory.view());
        equal(placed.workers, ids(64, 72));
        equal(locality(inventory.view(), placed.workers).racks, 1U);
    }
    equal(fragmentation(inventory.view()), 4U);
}
TEST(SchedulingTest, CompactPrefersFullNicAndRackFallbackIsDeterministic) {
    auto graph = topology::generate_clos({128, 8, 8, 8});
    ResourceInventory inventory{*graph};
    inventory.allocate(JobId{1}, ids(1, 8));
    equal(make_policy("compact", 42)->place({JobId{0}, 8, 0, {}}, inventory.view()).workers,
          ids(64, 72));
    inventory.allocate(JobId{2}, ids(64, 128));
    equal(make_policy("compact", 42)->place({JobId{0}, 8, 0, {}}, inventory.view()).workers,
          ids(8, 16));
    inventory.release(JobId{2});
    equal(make_policy("rack-local", 42)->place({JobId{0}, 100, 0, {}}, inventory.view()).workers,
          make_policy("first-fit", 42)->place({JobId{0}, 100, 0, {}}, inventory.view()).workers);
}
TEST(SchedulingTest, SeededRandomIsStableUniqueAndExcludesFailedGpus) {
    auto graph = topology::generate_clos({128, 8, 8, 8});
    ResourceInventory inventory{*graph};
    static_cast<void>(inventory.set_health(topology::GpuId{0}, false));
    const JobRequest request{JobId{7}, 32, 0, {}};
    const auto a = make_policy("random", 42)->place(request, inventory.view()).workers;
    equal(a, make_policy("random", 42)->place(request, inventory.view()).workers);
    equal(a == make_policy("random", 43)->place(request, inventory.view()).workers, false);
    equal(std::set<topology::GpuId>(a.begin(), a.end()).size(), 32U);
    equal(std::find(a.begin(), a.end(), topology::GpuId{0}) == a.end(), true);
}
TEST(SchedulingTest, QueueWaitIsExcludedFromAllocatedGpuIdle) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{100})};
    const auto report = workload::run_training(s);
    const auto& second = snapshot(report, 1);
    equal(second.state, JobState::Succeeded);
    equal(second.waiting_ns, 900U);
    equal(second.elapsed_ns, 1900U);
    equal(second.compute_gpu_ns, 1000U);
    equal(second.idle_gpu_ns, 0U);
    equal(second.allocated_at, std::optional{sim::SimTimeNs{1000}});
}
TEST(SchedulingTest, PriorityThenArrivalThenIdOrdersWaitingJobsWithoutPreemption) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{10}), pinned_job(0, sim::SimTimeNs{20}),
              pinned_job(0, sim::SimTimeNs{20})};
    s.jobs[2].priority = 5;
    s.jobs[3].priority = 5;
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).finished, std::optional{sim::SimTimeNs{1000}});
    equal(snapshot(report, 2).allocated_at, std::optional{sim::SimTimeNs{1000}});
    equal(snapshot(report, 3).allocated_at, std::optional{sim::SimTimeNs{2000}});
    equal(snapshot(report, 1).allocated_at, std::optional{sim::SimTimeNs{3000}});
}
TEST(SchedulingTest, BackfillDoesNotReserveCapacityForBlockedHead) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{10}), job(1, sim::SimTimeNs{20})};
    s.jobs[1].priority = 100;
    const auto report = workload::run_training(s);
    equal(snapshot(report, 2).waiting_ns, 0U);
    equal(snapshot(report, 2).allocated_at, std::optional{sim::SimTimeNs{20}});
    equal(snapshot(report, 1).waiting_ns, 990U);
}
TEST(SchedulingTest, FailedCapacityWaitsForRecoveryAndThenStarts) {
    auto s = scenario();
    s.jobs = {pinned_job()};
    s.gpu_controls = {{topology::GpuId{0}, false, sim::SimTimeNs{0}},
                      {topology::GpuId{0}, true, sim::SimTimeNs{500}}};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).waiting_ns, 500U);
    equal(snapshot(report, 0).finished, std::optional{sim::SimTimeNs{1500}});
}
TEST(SchedulingTest, AllocatedGpuFailureAbortsOwnerAndPreservesHealthUntilRecovery) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{100})};
    s.gpu_controls = {{topology::GpuId{0}, false, sim::SimTimeNs{400}},
                      {topology::GpuId{0}, true, sim::SimTimeNs{800}}};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).state, JobState::Failed);
    equal(snapshot(report, 0).compute_gpu_ns, 400U);
    equal(snapshot(report, 1).allocated_at, std::optional{sim::SimTimeNs{800}});
    equal(snapshot(report, 1).waiting_ns, 700U);
}
TEST(SchedulingTest, PermanentFailureLeavesExplicitWaitingResultAtQueueExhaustion) {
    auto s = scenario();
    s.jobs = {pinned_job()};
    s.gpu_controls = {{topology::GpuId{0}, false, sim::SimTimeNs{0}},
                      {topology::GpuId{1}, false, sim::SimTimeNs{500}}};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).state, JobState::Waiting);
    equal(snapshot(report, 0).finished.has_value(), false);
    equal(snapshot(report, 0).waiting_ns, 500U);
    equal(snapshot(report, 0).idle_gpu_ns, 0U);
}
TEST(SchedulingTest, ImpossibleJobFailsWithoutBlockingUsefulJobs) {
    auto s = scenario();
    s.jobs = {job(65), job()};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).state, JobState::Failed);
    equal(snapshot(report, 1).state, JobState::Succeeded);
}
TEST(SchedulingTest, CancellingWaitingJobDoesNotReleaseAnotherOwnersGpu) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{10}), pinned_job(0, sim::SimTimeNs{20})};
    s.controls = {{1, workload::WorkloadEventKind::Cancel, sim::SimTimeNs{400}, 0}};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 1).state, JobState::Cancelled);
    equal(snapshot(report, 1).waiting_ns, 390U);
    equal(snapshot(report, 1).compute_gpu_ns, 0U);
    equal(snapshot(report, 2).allocated_at, std::optional{sim::SimTimeNs{1000}});
}
TEST(SchedulingTest, CancellationReleasesAllocationAndPartialMetricsRemainCorrect) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{100})};
    s.controls = {{0, workload::WorkloadEventKind::Cancel, sim::SimTimeNs{400}, 0}};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).compute_gpu_ns, 400U);
    equal(snapshot(report, 1).waiting_ns, 300U);
}
TEST(SchedulingTest, PlacementChangesCollectivePathsAndRepeatsAllDomainRecords) {
    auto s = scenario();
    s.gpus = 128;
    s.jobs = {job(8)};
    configuration(s).policy = "compact";
    const auto compact_report = workload::run_training(s);
    configuration(s).policy = "random";
    const auto random = workload::run_training(s);
    const auto repeated = workload::run_training(s);
    equal(compact_report.decisions.empty(), true);
    equal(random.decisions.empty(), false);
    equal(random.placements, repeated.placements);
    equal(random.jobs, repeated.jobs);
    equal(random.timeline, repeated.timeline);
    equal(random.collectives, repeated.collectives);
    equal(random.decisions, repeated.decisions);
}
TEST(SchedulingTest, ScenarioSchemaAndReadableDecisionOutput) {
    const auto s = workload::parse_scenario(
        "version: 1\nscheduling_policy: compact\njobs:\n  - requested_workers: 2\n    compute_ns: "
        "[100, 200]\ngpu_controls:\n  - {gpu: 0, state: down, at_ns: 0}\n");
    equal(s.jobs[0].requested_workers, 2U);
    equal(s.jobs[0].workers.empty(), true);
    const auto report = workload::run_training(s);
    std::ostringstream output;
    workload::write_report(report, output, true);
    equal(output.str().find("placement job=0") != std::string::npos, true);
    equal(output.str().find("waiting_ns=") != std::string::npos, true);
}
TEST(SchedulingTest, InvalidSchedulingInputsAreRejected) {
    for (const auto* text :
         {"version: 1\njobs: [{requested_workers: 1}]",
          "version: 1\nscheduling_policy: invalid\njobs: [{workers: [0]}]",
          "version: 1\nscheduling_policy: first-fit\njobs: [{workers: [0], requested_workers: 1}]",
          "version: 1\nscheduling_policy: first-fit\njobs: [{requested_workers: 0}]",
          "version: 1\nscheduling_policy: first-fit\njobs: [{requested_workers: 8193}]",
          "version: 1\nscheduling_policy: first-fit\njobs: [{requested_workers: 2, compute_ns: "
          "[1]}]",
          "version: 1\njobs: [{workers: [0]}]\ngpu_controls: []",
          "version: 1\nscheduling_policy: first-fit\njobs: [{workers: [0]}]\ngpu_controls: [{gpu: "
          "64, state: up, at_ns: 0}]"}) {
        equal(throws<std::invalid_argument>(
                  [&] { static_cast<void>(workload::parse_scenario(text)); }),
              true);
    }
}
TEST(SchedulingTest, DecisionAndAllocationRetentionLimitsFailExplicitly) {
    auto s = scenario();
    configuration(s).decision_entries = 1;
    s.jobs = {pinned_job(), pinned_job()};
    equal(throws<std::runtime_error>([&] { static_cast<void>(workload::run_training(s)); }), true);
    configuration(s).decision_entries = 100;
    configuration(s).allocation_entries = 1;
    s.jobs = {job(2)};
    equal(throws<std::runtime_error>([&] { static_cast<void>(workload::run_training(s)); }), true);
}
TEST(SchedulingTest, RandomVersionOneHasGoldenRankOrder) {
    auto graph = topology::generate_clos({64, 8, 8, 8});
    ResourceInventory inventory{*graph};
    const auto result = make_policy("random", 42)->place({JobId{7}, 8, 0, {}}, inventory.view());
    const std::vector<topology::GpuId> expected{
        topology::GpuId{33}, topology::GpuId{61}, topology::GpuId{30}, topology::GpuId{2},
        topology::GpuId{44}, topology::GpuId{25}, topology::GpuId{35}, topology::GpuId{56}};
    equal(result.workers, expected);
}
TEST(SchedulingTest, WaitingArrivalOrderPrecedesInputId) {
    auto s = scenario();
    s.jobs = {pinned_job(), pinned_job(0, sim::SimTimeNs{200}), pinned_job(0, sim::SimTimeNs{100})};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 2).allocated_at, std::optional{sim::SimTimeNs{1000}});
    equal(snapshot(report, 1).allocated_at, std::optional{sim::SimTimeNs{2000}});
}
TEST(SchedulingTest, FullDynamicAllocationReleasesAndAdmitsNextJob) {
    auto s = scenario();
    s.jobs = {job(64), job(64, sim::SimTimeNs{1})};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 1).state, JobState::Succeeded);
    equal(snapshot(report, 1).allocated_at, snapshot(report, 0).finished);
    equal(snapshot(report, 1).waiting_ns,
          snapshot(report, 0).finished.value_or(sim::SimTimeNs{}).count() - 1);
}
TEST(SchedulingTest, CommunicationCancellationReleasesGpusWhileIssuedTrafficDrains) {
    auto s = scenario();
    auto first = job(2);
    first.requested_workers = 0;
    first.workers = {topology::GpuId{0}, topology::GpuId{8}};
    s.jobs = {first, pinned_job()};
    s.controls = {{0, workload::WorkloadEventKind::Cancel, sim::SimTimeNs{1001}, 0}};
    const auto report = workload::run_training(s);
    equal(snapshot(report, 0).compute_gpu_ns, 2000U);
    equal(snapshot(report, 1).allocated_at, std::optional{sim::SimTimeNs{1001}});
    equal(report.collectives[0].finished > snapshot(report, 0).finished.value_or(sim::SimTimeNs{}),
          true);
}
class InvalidPolicy final : public SchedulingPolicy {
  public:
    [[nodiscard]] Placement place(const JobRequest& /*request*/,
                                  ClusterResourceView /*resources*/) const override {
        return {PlacementOutcome::Placed,
                {topology::GpuId{0}, topology::GpuId{0}},
                "invalid duplicate"};
    }
};
TEST(SchedulingTest, CustomPolicyCannotInjectDuplicateAllocation) {
    auto graph = topology::generate_single_rack({2, 2});
    transport::TransportRuntime transport{*graph, {}};
    routing::Router router{*graph, transport, routing::PolicyRegistry{}};
    collective::RingExecutor ring{*graph, router};
    workload::WorkloadEngine jobs{
        *graph, ring, {}, Configuration{}, std::make_unique<InvalidPolicy>()};
    sim::Simulation simulation{42};
    workload::TrainingDispatcher dispatcher{jobs, ring, transport};
    static_cast<void>(jobs.schedule(job(2), simulation));
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Failed);
    equal(jobs.placements().empty(), true);
}
} // namespace
} // namespace nexuslab::scheduling
