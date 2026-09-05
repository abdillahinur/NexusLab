// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/topology/families.hpp"
#include "nexuslab/workload/dispatcher.hpp"
#include "nexuslab/workload/run.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <set>
#include <source_location>
#include <sstream>
namespace nexuslab::workload {
namespace {
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
template <typename T> T required(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"required test value absent"};
    }
    return *value;
}
std::vector<transport::DirectedLinkConfiguration> config(const topology::TopologyGraph& graph) {
    std::vector<transport::DirectedLinkConfiguration> links;
    for (const auto& link : graph.links()) {
        if (link.kind == topology::LinkKind::Fabric) {
            for (const auto& arc : topology::directed_links(link)) {
                links.push_back({arc.id, transport::BitsPerSecond{8'000'000'000ULL},
                                 sim::SimDurationNs{25}, transport::ByteCount{1'000'000},
                                 std::nullopt});
            }
        }
    }
    return links;
}
enum class Fabric : std::uint8_t { Direct, Local, Four };
std::unique_ptr<topology::TopologyGraph> graph_for(Fabric fabric) {
    if (fabric == Fabric::Local) {
        return topology::generate_single_rack({2, 2});
    }
    if (fabric == Fabric::Four) {
        return topology::generate_single_rack({4, 1});
    }
    return topology::generate_two_gpu_direct();
}
struct Fixture final {
    std::unique_ptr<topology::TopologyGraph> graph;
    transport::TransportRuntime transport;
    routing::Router router;
    collective::RingExecutor ring;
    WorkloadEngine jobs;
    sim::Simulation simulation{42};
    TrainingDispatcher dispatcher;
    explicit Fixture(Fabric fabric = Fabric::Direct)
        : graph{graph_for(fabric)}, transport{*graph, config(*graph)},
          router{*graph, transport, routing::PolicyRegistry{}},
          ring{
              *graph, router, {transport::BitsPerSecond{8'000'000'000ULL}, sim::SimDurationNs{25}}},
          jobs{*graph, ring}, dispatcher{jobs, ring, transport} {}
    static JobSpec spec(std::size_t workers = 2) {
        JobSpec result;
        result.name = "analytical";
        for (std::size_t i = 0; i < workers; ++i) {
            result.workers.emplace_back(i);
            result.compute.emplace_back(1000);
        }
        result.gradient_bytes = transport::ByteCount{100};
        result.bucket_bytes = transport::ByteCount{100};
        result.chunk_bytes = transport::ByteCount{100};
        return result;
    }
    JobSnapshot execute(JobSpec spec) {
        const auto id = jobs.schedule(std::move(spec), simulation);
        equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed);
        return required(jobs.snapshot(id, simulation.now()));
    }
};
TEST(TrainingTest, SingleWorkerHasOnlyComputeAndNoIdleOrNetwork) {
    Fixture f;
    auto spec = Fixture::spec(1);
    spec.steps = 3;
    const auto result = f.execute(spec);
    equal(result.state, JobState::Succeeded);
    equal(result.elapsed_ns, 3000U);
    equal(result.compute_gpu_ns, 3000U);
    equal(result.idle_gpu_ns, 0U);
    equal(f.router.decisions().size(), 0U);
    equal(f.jobs.take_completed().size(), 1U);
    equal(f.jobs.take_completed().size(), 0U);
}
TEST(TrainingTest, TwoWorkerIsolatedTimingAndGpuIdleMatchAnalyticalModel) {
    Fixture f;
    auto spec = Fixture::spec();
    spec.steps = 3;
    const auto result = f.execute(spec);
    equal(result.elapsed_ns, 3450U);
    equal(result.compute_gpu_ns, 6000U);
    equal(result.idle_gpu_ns, 900U);
    equal(result.completed_steps, 3U);
    const auto collective =
        required(required(f.ring.snapshot(collective::CollectiveId{0})).completion);
    equal(collective.planned_bytes, 200U);
    equal(collective.delivered_bytes, 200U);
    equal(collective.issued_fabric_bytes, 200U);
    equal(collective.issued_local_bytes, 0U);
}
TEST(TrainingTest, FourWorkerRingTraversesAllRoundsBeforeStepCompletes) {
    Fixture f{Fabric::Four};
    const auto result = f.execute(Fixture::spec(4));
    equal(result.elapsed_ns, 1600U);
    equal(result.compute_gpu_ns, 4000U);
    equal(result.idle_gpu_ns, 2400U);
    equal(f.ring.timeline().size(), 7U);
    equal(f.ring.timeline()[0].phase, collective::Phase::ReduceScatter);
    equal(f.ring.timeline()[3].phase, collective::Phase::AllGather);
    equal(
        required(required(f.ring.snapshot(collective::CollectiveId{0})).completion).delivered_bytes,
        600U);
}
TEST(TrainingTest, SameNicTrafficUsesExplicitLocalModel) {
    Fixture f{Fabric::Local};
    const auto result = f.execute(Fixture::spec());
    equal(result.elapsed_ns, 1150U);
    equal(f.router.decisions().size(), 0U);
    const auto collective =
        required(required(f.ring.snapshot(collective::CollectiveId{0})).completion);
    equal(collective.issued_local_bytes, 200U);
    equal(collective.issued_fabric_bytes, 0U);
}
TEST(TrainingTest, StragglerDeterminesBarrierAndIdleTime) {
    Fixture f;
    auto spec = Fixture::spec();
    spec.compute[1] = sim::SimDurationNs{2000};
    const auto result = f.execute(spec);
    equal(result.elapsed_ns, 2150U);
    equal(result.compute_gpu_ns, 3000U);
    equal(result.idle_gpu_ns, 1300U);
}
TEST(TrainingTest, BucketOverlapShortensCriticalPathWithoutDoubleCountingCompute) {
    Fixture sequential;
    auto spec = Fixture::spec();
    spec.bucket_bytes = transport::ByteCount{50};
    const auto baseline = sequential.execute(spec);
    Fixture overlap;
    spec.overlap = true;
    const auto result = overlap.execute(spec);
    equal(baseline.elapsed_ns, 1200U);
    equal(result.elapsed_ns, 1100U);
    equal(result.compute_gpu_ns, 2000U);
    equal(result.idle_gpu_ns, 200U);
    equal(overlap.ring.timeline().front().timestamp, sim::SimTimeNs{500});
}
TEST(TrainingTest, UnevenAndSubParticipantGradientSizesPreserveBytes) {
    for (const std::uint64_t bytes : {1U, 3U, 101U}) {
        Fixture f{Fabric::Four};
        auto spec = Fixture::spec(4);
        spec.gradient_bytes = transport::ByteCount{bytes};
        spec.bucket_bytes = transport::ByteCount{bytes};
        equal(f.execute(spec).state, JobState::Succeeded);
        equal(required(required(f.ring.snapshot(collective::CollectiveId{0})).completion)
                  .delivered_bytes,
              6 * bytes);
    }
}
TEST(TrainingTest, StaggeredDisjointJobsCanOverlap) {
    Fixture f{Fabric::Four};
    auto first = Fixture::spec();
    auto second = Fixture::spec();
    second.workers = {topology::GpuId{2}, topology::GpuId{3}};
    second.arrival = sim::SimTimeNs{100};
    const auto a = f.jobs.schedule(first, f.simulation);
    const auto b = f.jobs.schedule(second, f.simulation);
    equal(f.simulation.run(f.dispatcher).status, sim::SimulationStatus::Completed);
    equal(required(f.jobs.snapshot(a, f.simulation.now())).state, JobState::Succeeded);
    equal(required(f.jobs.snapshot(b, f.simulation.now())).state, JobState::Succeeded);
    equal(required(f.jobs.snapshot(b, f.simulation.now())).elapsed_ns, 1300U);
}
TEST(TrainingTest, ConflictingAssignmentsFailWithoutHiddenScheduling) {
    Fixture f;
    static_cast<void>(f.jobs.schedule(Fixture::spec(), f.simulation));
    const auto conflict = f.jobs.schedule(Fixture::spec(), f.simulation);
    equal(f.simulation.run(f.dispatcher).status, sim::SimulationStatus::Completed);
    const auto result = required(f.jobs.snapshot(conflict, f.simulation.now()));
    equal(result.state, JobState::Failed);
    equal(result.compute_gpu_ns, 0U);
    equal(result.idle_gpu_ns, 0U);
}
TEST(TrainingTest, CancellationAndWorkerFailureAccountPartialCompute) {
    for (const auto kind : {WorkloadEventKind::Cancel, WorkloadEventKind::WorkerFailure}) {
        Fixture f;
        const auto id = f.jobs.schedule(Fixture::spec(), f.simulation);
        static_cast<void>(f.jobs.schedule_control(id, kind, sim::SimTimeNs{400}, f.simulation));
        const auto run = f.simulation.run(f.dispatcher);
        equal(run.status, sim::SimulationStatus::Completed);
        equal(run.cancelled_events, 2U);
        const auto result = required(f.jobs.snapshot(id, f.simulation.now()));
        equal(result.elapsed_ns, 400U);
        equal(result.compute_gpu_ns, 800U);
        equal(result.idle_gpu_ns, 0U);
        equal(result.state,
              kind == WorkloadEventKind::Cancel ? JobState::Cancelled : JobState::Failed);
    }
}
TEST(TrainingTest, PreArrivalCancellationCancelsArrivalWithoutAllocatingGpuTime) {
    Fixture f;
    auto spec = Fixture::spec();
    spec.arrival = sim::SimTimeNs{1000};
    const auto id = f.jobs.schedule(spec, f.simulation);
    static_cast<void>(
        f.jobs.schedule_control(id, WorkloadEventKind::Cancel, sim::SimTimeNs{100}, f.simulation));
    equal(f.simulation.run(f.dispatcher).status, sim::SimulationStatus::Completed);
    const auto result = required(f.jobs.snapshot(id, f.simulation.now()));
    equal(result.elapsed_ns, 0U);
    equal(result.compute_gpu_ns, 0U);
    equal(result.idle_gpu_ns, 0U);
}
TEST(TrainingTest, CancelDuringCommunicationDrainsOnlyIssuedRoundAndAllowsGpuReuse) {
    Fixture f;
    const auto first = f.jobs.schedule(Fixture::spec(), f.simulation);
    static_cast<void>(f.jobs.schedule_control(first, WorkloadEventKind::Cancel,
                                              sim::SimTimeNs{1020}, f.simulation));
    auto next = Fixture::spec();
    next.arrival = sim::SimTimeNs{1021};
    const auto second = f.jobs.schedule(next, f.simulation);
    equal(f.simulation.run(f.dispatcher).status, sim::SimulationStatus::Completed);
    equal(required(f.jobs.snapshot(first, f.simulation.now())).state, JobState::Cancelled);
    equal(required(f.jobs.snapshot(second, f.simulation.now())).state, JobState::Succeeded);
    const auto result = required(required(f.ring.snapshot(collective::CollectiveId{0})).completion);
    equal(result.outcome, collective::Phase::Cancelled);
    equal(result.issued_fabric_bytes, 100U);
    equal(result.finished, sim::SimTimeNs{1075});
}
TEST(TrainingTest, FabricFailureStopsJobAndNoFurtherRoundsAreIssued) {
    Fixture f;
    const auto id = f.jobs.schedule(Fixture::spec(), f.simulation);
    const auto link = f.graph->links().back().id;
    static_cast<void>(f.simulation.schedule(
        {sim::SimTimeNs{1020}, sim::EventPriority::Critical,
         transport::LinkStateChangeEvent{link, topology::OperationalState::Down}}));
    equal(f.simulation.run(f.dispatcher).status, sim::SimulationStatus::Completed);
    equal(required(f.jobs.snapshot(id, f.simulation.now())).state, JobState::Failed);
    const auto result = required(required(f.ring.snapshot(collective::CollectiveId{0})).completion);
    equal(result.outcome, collective::Phase::Failed);
    equal(result.issued_fabric_bytes, 100U);
    equal(result.delivered_bytes, 0U);
}
TEST(TrainingTest, InvalidInputsAndLimitsRejectBeforeJobIdsAreConsumed) {
    Fixture f;
    auto invalid = Fixture::spec();
    invalid.compute[0] = sim::SimDurationNs{0};
    equal(throws<std::invalid_argument>(
              [&] { static_cast<void>(f.jobs.schedule(invalid, f.simulation)); }),
          true);
    invalid = Fixture::spec();
    invalid.workers[1] = invalid.workers[0];
    equal(throws<std::invalid_argument>(
              [&] { static_cast<void>(f.jobs.schedule(invalid, f.simulation)); }),
          true);
    invalid = Fixture::spec();
    invalid.gradient_bytes = transport::ByteCount{5000};
    invalid.bucket_bytes = transport::ByteCount{1};
    equal(throws<std::length_error>(
              [&] { static_cast<void>(f.jobs.schedule(invalid, f.simulation)); }),
          true);
    invalid = Fixture::spec();
    invalid.compute[0] = sim::SimDurationNs{std::numeric_limits<std::uint64_t>::max()};
    invalid.steps = 2;
    equal(throws<std::overflow_error>(
              [&] { static_cast<void>(f.jobs.schedule(invalid, f.simulation)); }),
          true);
    equal(f.jobs.schedule(Fixture::spec(), f.simulation), JobId{0});
}
TEST(TrainingTest, GlobalWorkerBudgetBoundsRetainedJobAssignments) {
    Fixture f;
    WorkloadLimits limits;
    limits.worker_entries = 2;
    WorkloadEngine jobs{*f.graph, f.ring, limits};
    static_cast<void>(jobs.schedule(Fixture::spec(), f.simulation));
    equal(throws<std::length_error>(
              [&] { static_cast<void>(jobs.schedule(Fixture::spec(), f.simulation)); }),
          true);
}
void verify_ring(std::uint32_t participants) {

    std::vector<std::vector<std::set<std::uint32_t>>> values(
        participants, std::vector<std::set<std::uint32_t>>(participants));
    for (std::uint32_t rank = 0; rank < participants; ++rank) {
        for (auto& shard : values[rank]) {
            shard.insert(rank);
        }
    }
    std::uint64_t volume{0};
    for (const auto phase : {collective::Phase::ReduceScatter, collective::Phase::AllGather}) {
        for (std::uint32_t round = 0; round < participants - 1; ++round) {
            auto next = values;
            for (const auto& transfer :
                 collective::plan_round(participants, transport::ByteCount{17}, phase, round)) {
                const auto& source = values[transfer.source][transfer.shard];
                auto& destination = next[transfer.destination][transfer.shard];
                if (phase == collective::Phase::ReduceScatter) {
                    destination.insert(source.begin(), source.end());
                } else {
                    destination = source;
                }
                volume += transfer.bytes.value();
            }
            values = std::move(next);
        }
    }
    equal(volume, 2ULL * (participants - 1) * 17);
    for (const auto& rank : values) {
        for (const auto& shard : rank) {
            equal(shard.size(), participants);
        }
    }
}
TEST(RingPlannerTest, SymbolicReduceScatterAndAllGatherCoverEveryParticipant) {
    for (const std::uint32_t participants : {2U, 4U, 8U}) {
        verify_ring(participants);
    }
}
TEST(RingPlannerTest, RejectsInvalidRoundsAndVolumeOverflow) {
    equal(throws<std::invalid_argument>([] {
              static_cast<void>(collective::plan_round(1, transport::ByteCount{1},
                                                       collective::Phase::ReduceScatter, 0));
          }),
          true);
    equal(throws<std::invalid_argument>([] {
              static_cast<void>(collective::plan_round(4, transport::ByteCount{1},
                                                       collective::Phase::AllGather, 3));
          }),
          true);
    equal(throws<std::overflow_error>([] {
              static_cast<void>(collective::planned_volume(
                  4, transport::ByteCount{std::numeric_limits<std::uint64_t>::max()}));
          }),
          true);
}
TEST(TrainingScenarioTest, StrictSchemaAndProfilesAreDeterministic) {
    const auto scenario =
        parse_scenario("version: 1\njobs: [{workers: [0,8], compute_ns: [100,200], steps: 1, "
                       "gradient_bytes: 100, bucket_bytes: 50, overlap: true}]\n");
    equal(scenario.jobs[0].compute[1], sim::SimDurationNs{200});
    equal(profiles().size(), 7U);
    const auto first = run_training(scenario);
    const auto second = run_training(scenario);
    equal(first.jobs, second.jobs);
    equal(first.collectives, second.collectives);
    equal(first.timeline, second.timeline);
    equal(first.decisions, second.decisions);
    std::ostringstream out;
    write_report(first, out, true);
    equal(out.str().find("job_event") != std::string::npos, true);
}
TEST(TrainingScenarioTest, RejectsUnknownDuplicateUnsupportedAndOversizedInputs) {
    for (const auto* yaml :
         {"version: 2\njobs: []", "version: 1\nversion: 1\njobs: []",
          "version: 1\nunknown: 2\njobs: []", "version: 1\njobs: [{workers: [0], algorithm: tree}]",
          "version: 1\njobs: [{workers: [0], compute_ns: -1}]",
          "version: 1\njobs: [{workers: [0], overlap: yes}]"}) {
        equal(throws<std::exception>([&] { static_cast<void>(parse_scenario(yaml)); }), true);
    }
    equal(throws<std::length_error>(
              [] { static_cast<void>(parse_scenario(std::string(1'048'577, ' '))); }),
          true);
}
TEST(TrainingTest, NewTypedEventsKeepEnvelopeBoundedAndStableTraceKinds) {
    equal(sizeof(sim::Event) <= 80, true);
    equal(sim::payload_kind(WorkloadEvent{JobId{0}, WorkloadEventKind::Arrival}),
          sim::EventPayloadKind::Workload);
    equal(sim::payload_kind(collective::LocalCompletionEvent{collective::CollectiveId{0}, 0, 0,
                                                             collective::Phase::ReduceScatter}),
          sim::EventPayloadKind::LocalCollectiveCompletion);
}
struct StopAfterCompute final {
    TrainingDispatcher& delegate;
    void operator()(const WorkloadEvent& event, sim::SimulationContext& context) const {
        delegate(event, context);
        if (event.kind == WorkloadEventKind::ComputeReady) {
            context.stop(sim::StopReason::Requested);
        }
    }
    template <typename Event>
    void operator()(const Event& event, sim::SimulationContext& context) const {
        delegate(event, context);
    }
};
TEST(TrainingTest, StoppedRunExposesPartialComputeWithoutInventingCompletion) {
    Fixture f;
    auto spec = Fixture::spec();
    spec.compute[0] = sim::SimDurationNs{400};
    const auto id = f.jobs.schedule(spec, f.simulation);
    StopAfterCompute dispatcher{f.dispatcher};
    equal(f.simulation.run(dispatcher).status, sim::SimulationStatus::Stopped);
    const auto result = required(f.jobs.snapshot(id, f.simulation.now()));
    equal(result.compute_gpu_ns, 800U);
    equal(result.idle_gpu_ns, 0U);
    equal(result.completed_steps, 0U);
    equal(f.jobs.take_completed().empty(), true);
}
TEST(TrainingTest, NoRouteAndBufferLossAreExplicitJobFailures) {
    Fixture f;
    equal(f.graph->set_link_state(f.graph->links().back().id, topology::OperationalState::Down),
          true);
    equal(f.execute(Fixture::spec()).state, JobState::Failed);
    equal(f.router.decisions().size(), 1U);
    const auto scenario =
        parse_scenario("version: 1\nbuffer_bytes: 0\njobs: [{workers: [0,8], steps: 1, compute_ns: "
                       "100, gradient_bytes: 65536, chunk_bytes: 1024}]\n");
    const auto report = run_training(scenario);
    equal(report.jobs.front().state, JobState::Failed);
    equal(report.jobs.front().completed_steps, 0U);
}
TEST(TrainingTest, LocalDelayAndTimelineLimitsFailExplicitly) {
    const std::string_view yaml =
        "version: 1\nlocal_latency_ns: 18446744073709551615\njobs: [{workers: [0,1], steps: 1}]\n";
    equal(
        throws<std::runtime_error>([&] { static_cast<void>(run_training(parse_scenario(yaml))); }),
        true);
    Fixture f;
    WorkloadLimits limits;
    limits.timeline_entries = 1;
    WorkloadEngine jobs{*f.graph, f.ring, limits};
    TrainingDispatcher dispatcher{jobs, f.ring, f.transport};
    static_cast<void>(jobs.schedule(Fixture::spec(), f.simulation));
    equal(f.simulation.run(dispatcher).status, sim::SimulationStatus::Failed);
}
TEST(TrainingScenarioTest, BoundsScalarSizesBeforeAliasExpansionCanAmplifyCopies) {
    const auto name_yaml =
        "version: 1\njobs: [{workers: [0], name: " + std::string(257, 'x') + "}]\n";
    equal(throws<std::length_error>([&] { static_cast<void>(parse_scenario(name_yaml)); }), true);
    equal(throws<std::invalid_argument>([] {
              static_cast<void>(
                  parse_scenario("version: 000000000000000000001\njobs: [{workers: [0]}]\n"));
          }),
          true);
}
TEST(TrainingScenarioTest, DisjointGpusSharingNicsExposeNetworkContention) {
    const auto isolated = parse_scenario("version: 1\njobs: [{workers: [0,8], steps: 1}]\n");
    auto shared = isolated;
    auto second = shared.jobs.front();
    second.workers = {topology::GpuId{1}, topology::GpuId{9}};
    shared.jobs.push_back(second);
    const auto baseline = run_training(isolated);
    const auto result = run_training(shared);
    equal(result.jobs.size(), 2U);
    equal(result.jobs.back().state, JobState::Succeeded);
    equal(result.jobs.back().elapsed_ns > baseline.jobs.front().elapsed_ns, true);
    equal(result.maximum_waiting_bytes > baseline.maximum_waiting_bytes, true);
}
} // namespace
} // namespace nexuslab::workload
