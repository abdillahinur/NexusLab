// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/cli/application.hpp"
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/traffic/runtime.hpp"
#include "nexuslab/workload/digest.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <source_location>
#include <sstream>
namespace nexuslab::traffic {
namespace {
template <typename A, typename B>
void equal(const A& a, const B& b,
           std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_EQ(a, b);
}
template <typename A, typename B> void greater(const A& a, const B& b) { EXPECT_GT(a, b); }
template <typename T> T required(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"missing test value"};
    }
    return *value;
}
template <typename Exception, typename Operation> void rejects(Operation operation) {
    bool caught = false;
    try {
        operation();
    } catch (const Exception&) {
        caught = true;
    } catch (...) {
        caught = false;
    }
    EXPECT_TRUE(caught);
}
Source source(std::uint64_t count = 2, std::uint64_t interval = 5000) {
    return {Kind::Burst,
            topology::GpuId{0},
            topology::GpuId{1},
            sim::SimTimeNs{0},
            count,
            sim::SimDurationNs{interval},
            transport::ByteCount{1000},
            transport::ByteCount{1000}};
}
std::vector<transport::DirectedLinkConfiguration>
configuration(const topology::TopologyGraph& graph, std::uint64_t buffer = 10000) {
    std::vector<transport::DirectedLinkConfiguration> result;
    for (const auto& link : graph.links()) {
        if (link.kind != topology::LinkKind::Fabric) {
            continue;
        }
        for (const auto& arc : topology::directed_links(link)) {
            result.push_back({arc.id, transport::BitsPerSecond{8'000'000'000ULL},
                              sim::SimDurationNs{25}, transport::ByteCount{buffer}, std::nullopt});
        }
    }
    return result;
}
struct Dispatcher final {
    transport::TransportRuntime& transport;
    Runtime& loads;
    failures::Runtime& failures;
    void drain() {
        for (const auto& completion : transport.take_completed_transfers()) {
            loads.handle(completion);
        }
    }
    void operator()(const InjectionEvent& e, sim::SimulationContext& c) {
        loads.handle(e, c);
        drain();
    }
    void operator()(const transport::ChunkArrivalEvent& e, sim::SimulationContext& c) {
        transport.handle_arrival(e, c);
        drain();
    }
    void operator()(const transport::TransmissionCompleteEvent& e, sim::SimulationContext& c) {
        transport.handle_completion(e, c);
        drain();
    }
    void operator()(const transport::LinkStateChangeEvent& e, sim::SimulationContext& c) {
        failures.handle(e, c);
        drain();
    }
    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& c) const {
        c.stop(sim::StopReason::Requested);
    }
    template <typename T>
    void operator()(const T& /*event*/, sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected test event"};
    }
};
struct Fixture final {
    std::unique_ptr<topology::TopologyGraph> graph{topology::generate_two_gpu_direct()};
    transport::TransportRuntime transport;
    routing::Router router;
    failures::Runtime failures;
    Runtime loads;
    sim::Simulation simulation{42};
    Dispatcher dispatcher{transport, loads, failures};
    explicit Fixture(std::vector<Source> sources, const failures::Script& script = {},
                     std::uint64_t buffer = 10000)
        : transport{*graph, configuration(*graph, buffer)},
          router{*graph, transport, routing::PolicyRegistry{}}, failures{script, *graph, transport},
          loads{std::move(sources), *graph, router} {
        failures.schedule(simulation);
        loads.schedule(simulation);
    }
    void run() { equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed); }
};
TEST(TrafficTest, PacesOneHopTransfersAndKeepsSourceOwnership) {
    Fixture fixture{{source()}};
    fixture.run();
    const auto& requests = fixture.loads.report();
    equal(requests.size(), 2U);
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        equal(request.source.value(), 1U);
        equal(request.ordinal, i);
        equal(required(request.injected).count(), i * 5000);
        equal(required(request.finished).count(), i * 5000 + 1025);
        equal(request.outcome, Outcome::Succeeded);
        equal(request.delivered.bytes, 1000U);
        equal(request.dropped_buffer_full.bytes + request.dropped_link_down.bytes, 0U);
    }
    equal(fixture.router.decisions().front().request.flow, std::uint64_t{1} << 63U);
}
TEST(TrafficTest, OrdersZeroIntervalSourcesAndReportsBufferLoss) {
    auto checkpoint = source(1, 0);
    checkpoint.kind = Kind::Checkpoint;
    Fixture fixture{{source(2, 0), checkpoint}, {}, 1000};
    fixture.run();
    const auto& requests = fixture.loads.report();
    equal(required(requests.at(0).transfer).value(), 0U);
    equal(required(requests.at(1).transfer).value(), 1U);
    equal(required(requests.at(2).transfer).value(), 2U);
    equal(requests.at(0).outcome, Outcome::Succeeded);
    equal(requests.at(1).outcome, Outcome::Succeeded);
    equal(requests.at(2).outcome, Outcome::Failed);
    equal(requests.at(2).dropped_buffer_full.bytes, 1000U);
    equal(fixture.failures.report().incidents.size(), 0U);
}
TEST(TrafficTest, DistinguishesInFlightDropsNoRouteAndRecovery) {
    Fixture fixture{
        {source(3, 1000)},
        failures::Script{{{topology::LinkId{2}, sim::SimTimeNs{500}, sim::SimTimeNs{2000}}}}};
    fixture.run();
    const auto& requests = fixture.loads.report();
    equal(requests.at(0).outcome, Outcome::Failed);
    equal(requests.at(0).dropped_link_down.bytes, 1000U);
    equal(requests.at(1).outcome, Outcome::NoRoute);
    equal(requests.at(1).transfer.has_value(), false);
    equal(requests.at(1).delivered.bytes + requests.at(1).dropped_link_down.bytes, 0U);
    equal(requests.at(2).outcome, Outcome::Succeeded);
    const auto report = fixture.failures.report();
    equal(report.incidents.front().dropped.bytes, 1000U);
    equal(report.incidents.front().affected_jobs.size(), 0U);
    equal(report.drops.front().job.has_value(), false);
}
TEST(TrafficTest, StoppedSimulationRetainsInjectedAndUninjectedPendingRequests) {
    Fixture fixture{{source()}};
    static_cast<void>(fixture.simulation.schedule(
        {sim::SimTimeNs{500}, sim::EventPriority::Normal, sim::NoOpEvent{}}));
    equal(fixture.simulation.run(fixture.dispatcher).status, sim::SimulationStatus::Stopped);
    const auto& requests = fixture.loads.report();
    equal(requests.front().outcome, Outcome::Pending);
    equal(requests.front().transfer.has_value(), true);
    equal(requests.back().outcome, Outcome::Pending);
    equal(requests.back().injected.has_value(), false);
    equal(requests.front().finished.has_value(), false);
}
TEST(TrafficTest, RejectsInvalidEndpointsSizesAndBoundsBeforeScheduling) {
    const auto graph = topology::generate_two_gpu_direct();
    auto invalid = source();
    invalid.destination = invalid.source;
    rejects<std::invalid_argument>([&] { static_cast<void>(validate({invalid}, *graph)); });
    invalid = source();
    invalid.source = topology::GpuId{99};
    rejects<std::invalid_argument>([&] { static_cast<void>(validate({invalid}, *graph)); });
    invalid = source();
    invalid.count = 0;
    rejects<std::invalid_argument>([&] { static_cast<void>(validate({invalid}, *graph)); });
    invalid = source();
    invalid.bytes = transport::ByteCount{0};
    rejects<std::invalid_argument>([&] { static_cast<void>(validate({invalid}, *graph)); });
    invalid = source();
    invalid.chunk_bytes = transport::ByteCount{0};
    rejects<std::invalid_argument>([&] { static_cast<void>(validate({invalid}, *graph)); });
    rejects<std::length_error>([&] { static_cast<void>(validate({source()}, *graph, {1, 1})); });
    rejects<std::length_error>(
        [&] { static_cast<void>(validate({source(), source()}, *graph, {1, 10})); });
    const auto shared = topology::generate_single_rack({2, 2});
    rejects<std::invalid_argument>([&] { static_cast<void>(validate({source()}, *shared)); });
}
TEST(TrafficTest, RejectsScheduleAndAggregateByteOverflow) {
    const auto graph = topology::generate_two_gpu_direct();
    auto invalid = source();
    invalid.onset = sim::SimTimeNs{UINT64_MAX};
    rejects<std::overflow_error>([&] { static_cast<void>(validate({invalid}, *graph)); });
    invalid = source();
    invalid.bytes = transport::ByteCount{UINT64_MAX};
    rejects<std::overflow_error>([&] { static_cast<void>(validate({invalid}, *graph)); });
    invalid.count = 1;
    rejects<std::overflow_error>([&] { static_cast<void>(validate({invalid, invalid}, *graph)); });
    equal(validate({source(1, UINT64_MAX)}, *graph).size(), 1U);
}
std::string scenario_yaml() {
    return R"(version: 1
gpus: 64
routing_policy: shortest-path
bandwidth_bps: 8000000000
propagation_ns: 25
buffer_bytes: 100000
jobs:
  - name: contention
    workers: [0, 8]
    steps: 1
    compute_ns: 1000
    gradient_bytes: 4000
    bucket_bytes: 4000
    chunk_bytes: 1000
failure_script:
  version: 1
  detection: immediate
  incidents: []
  loads:
    - {kind: checkpoint, source_gpu: 0, destination_gpu: 8, at_ns: 1000, count: 2, interval_ns: 0, bytes_per_transfer: 8000, chunk_bytes: 1000}
)";
}
TEST(TrafficTest, ContendsWithTrainingWithoutPassingLoadCompletionsToRing) {
    auto scenario = workload::parse_scenario(scenario_yaml());
    const auto loaded = workload::run_training(scenario);
    scenario.failure_script.reset();
    const auto baseline = workload::run_training(scenario);
    equal(loaded.jobs.front().state, workload::JobState::Succeeded);
    equal(loaded.collectives.size(), baseline.collectives.size());
    equal(loaded.loads.size(), 2U);
    equal(required(loaded.failures).outcome_version, 4U);
    greater(loaded.jobs.front().elapsed_ns, baseline.jobs.front().elapsed_ns);
    equal(loaded.loads.front().outcome, Outcome::Succeeded);
    equal(loaded.loads.back().delivered.bytes, 8000U);
}
TEST(TrafficTest, RepeatsAcrossTelemetryModesAndHashesLoadOutcomes) {
    auto scenario = workload::parse_scenario(scenario_yaml());
    const auto baseline = workload::run_training(scenario);
    const auto digest = workload::domain_outcome_digest(baseline);
    for (const auto mode : {telemetry::TelemetryMode::Off, telemetry::TelemetryMode::Summary,
                            telemetry::TelemetryMode::Sampled, telemetry::TelemetryMode::Full}) {
        scenario.telemetry.mode = mode;
        const auto repeated = workload::run_training(scenario);
        equal(workload::domain_outcome_digest(repeated), digest);
        equal(repeated.loads, baseline.loads);
    }
    auto changed = baseline;
    changed.loads.front().outcome = Outcome::Failed;
    equal(workload::domain_outcome_digest(changed) == digest, false);
}
TEST(TrafficTest, CanonicalScriptDigestIncludesLoadProvenanceAndSchedule) {
    const auto graph = topology::generate_two_gpu_direct();
    failures::Script script{{}, {source()}};
    const auto digest = failures::script_digest(script, *graph);
    // Independent little-endian FNV vector recorded with the slice evidence.
    equal(digest, std::uint64_t{0x0fda0f6882b57b1aULL});
    script.loads.front().kind = Kind::Checkpoint;
    equal(failures::script_digest(script, *graph) == digest, false);
    script.loads.front().interval = sim::SimDurationNs{100};
    equal(failures::script_digest(script, *graph) == digest, false);
}
TEST(TrafficTest, CliReturnsFailureForLostLoadsEvenWhenTrainingSucceeds) {
    auto yaml = scenario_yaml();
    const auto position = yaml.find("buffer_bytes: 100000");
    yaml.replace(position, std::string{"buffer_bytes: 100000"}.size(), "buffer_bytes: 1");
    // Keep training local so its success is independent of the fabric loss.
    yaml.replace(yaml.find("workers: [0, 8]"), std::string{"workers: [0, 8]"}.size(),
                 "workers: [16]");
    const auto report = workload::run_training(workload::parse_scenario(yaml));
    equal(report.jobs.front().state, workload::JobState::Succeeded);
    equal(report.loads.front().outcome, Outcome::Failed);
    const auto path = std::filesystem::temp_directory_path() / "nexuslab-background-cli.yaml";
    {
        std::ofstream file{path};
        file << yaml;
    }
    const auto filename = path.string();
    const std::array<std::string_view, 4> arguments{"train", "--file", filename, "--timeline"};
    std::ostringstream output;
    std::ostringstream error;
    equal(cli::run(arguments, output, error), 1);
    equal(output.str().find("outcome=failed") != std::string::npos, true);
    std::filesystem::remove(path);
}
TEST(TrafficTest, PreflightsSerializationBeforeRegisteringEvents) {
    const auto graph = topology::generate_two_gpu_direct();
    transport::TransportRuntime transport{*graph, configuration(*graph)};
    routing::Router router{*graph, transport, routing::PolicyRegistry{}};
    auto invalid = source(1, 0);
    invalid.onset = sim::SimTimeNs{UINT64_MAX - 10};
    rejects<std::overflow_error>([&] { Runtime loads{{invalid}, *graph, router}; });
    equal(router.decisions().size(), 0U);
}
TEST(TrafficTest, ParserRejectsUnknownAndMissingLoadFields) {
    const auto yaml = scenario_yaml();
    auto invalid = yaml;
    invalid.replace(invalid.find("kind: checkpoint"), 16, "kind: unknown");
    rejects<std::invalid_argument>([&] { static_cast<void>(workload::parse_scenario(invalid)); });
    invalid = yaml;
    invalid.replace(invalid.find("count: 2"), 8, "unknown: 2");
    rejects<std::invalid_argument>([&] { static_cast<void>(workload::parse_scenario(invalid)); });
}
} // namespace
} // namespace nexuslab::traffic
