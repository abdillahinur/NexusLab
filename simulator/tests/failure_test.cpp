// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/cli/application.hpp"
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/workload/digest.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <source_location>
#include <sstream>

namespace nexuslab::failures {
namespace {
template <typename A, typename B>
void equal(const A& a, const B& b,
           std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_EQ(a, b);
}
template <typename A, typename B>
void different(const A& a, const B& b,
               std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_NE(a, b);
}
template <typename A, typename B>
void greater(const A& a, const B& b,
             std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_GT(a, b);
}
void truth(bool value, std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_TRUE(value);
}
void falsity(bool value, std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_FALSE(value);
}
template <typename Exception, typename Operation>
void expect_throw(Operation operation,
                  std::source_location location = std::source_location::current()) {
    bool caught = false;
    try {
        operation();
    } catch (const Exception&) {
        caught = true;
    } catch (...) {
        caught = false;
    }
    truth(caught, location);
}
template <typename T> T required(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"missing required test value"};
    }
    return *value;
}
std::vector<transport::DirectedLinkConfiguration>
configuration(const topology::TopologyGraph& graph) {
    std::vector<transport::DirectedLinkConfiguration> result;
    for (const auto& link : graph.links()) {
        if (link.kind == topology::LinkKind::Fabric) {
            for (const auto& arc : topology::directed_links(link)) {
                result.push_back({arc.id, transport::BitsPerSecond{8'000'000'000ULL},
                                  sim::SimDurationNs{25}, transport::ByteCount{10'000},
                                  std::nullopt});
            }
        }
    }
    return result;
}
struct Dispatcher final {
    transport::TransportRuntime& transport;
    Runtime& failures;
    std::function<void(sim::SimulationContext&)> submit;
    void operator()(const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) const {
        submit(context);
    }
    void operator()(const transport::ChunkArrivalEvent& e, sim::SimulationContext& c) {
        transport.handle_arrival(e, c);
    }
    void operator()(const transport::TransmissionCompleteEvent& e, sim::SimulationContext& c) {
        transport.handle_completion(e, c);
    }
    void operator()(const transport::LinkStateChangeEvent& e, sim::SimulationContext& c) {
        failures.handle(e, c);
    }
    template <class Event>
    void operator()(const Event& /*event*/, sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected fixture event"};
    }
};
TEST(FailureTest, ActiveQueuedAndSameTimeCompletionDropOnceThenNewWorkSucceeds) {
    const auto graph = topology::generate_two_gpu_direct();
    const auto configs = configuration(*graph);
    const auto link = configs.front().link;
    transport::TransportRuntime transport{*graph, configs};
    Runtime failures{Script{{{link.link, sim::SimTimeNs{1000}, sim::SimTimeNs{2000}}}}, *graph,
                     transport};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    std::vector<transport::TransferId> ids;
    Dispatcher dispatcher{transport, failures, [&](sim::SimulationContext& context) {
                              ids.push_back(
                                  transport
                                      .submit_transfer({topology::NodeId{topology::NicId{0}},
                                                        topology::NodeId{topology::NicId{1}},
                                                        transport::ByteCount{2500},
                                                        transport::ByteCount{1000},
                                                        {link}},
                                                       context)
                                      .id);
                          }};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{2000}, sim::EventPriority::Normal, sim::NoOpEvent{1}}));
    const auto result = simulation.run(dispatcher);
    equal(result.status, sim::SimulationStatus::Completed);
    equal(ids.size(), 2U);
    const auto first = required(transport.transfer_snapshot(ids.at(0)));
    const auto second = required(transport.transfer_snapshot(ids.at(1)));
    equal(first.dropped_link_down.bytes, 2500U);
    equal(first.dropped_link_down.chunks, 3U);
    equal(first.delivered.bytes, 0U);
    equal(first.total, first.dropped_link_down);
    equal(required(second.completion).outcome, transport::TransferOutcome::Succeeded);
    equal(second.delivered.bytes, 2500U);
    const auto report = failures.report();
    equal(report.incidents.size(), 1U);
    equal(report.incidents.at(0).dropped, first.dropped_link_down);
    truth(report.incidents.at(0).recovered);
    truth(report.incidents.at(0).effective_operational);
    equal(report.timeline.size(), 5U);
    equal(report.timeline.at(1).kind, telemetry::FailureTransition::Applied);
    equal(report.timeline.at(2).kind, telemetry::FailureTransition::Detected);
    equal(report.timeline.at(1).timestamp, sim::SimTimeNs{1000});
    equal(report.timeline.at(2).timestamp, sim::SimTimeNs{1000});
    equal(report.timeline.back().timestamp, sim::SimTimeNs{2000});
    equal(report.drops.size(), 3U);
    equal(transport.take_completed_transfers().size(), 2U);
    truth(transport.take_completed_transfers().empty());
}
TEST(FailureTest, PropagatingChunkSurvivesPermanentFailure) {
    const auto graph = topology::generate_two_gpu_direct();
    const auto configs = configuration(*graph);
    const auto link = configs.front().link;
    transport::TransportRuntime transport{*graph, configs};
    Runtime failures{Script{{{link.link, sim::SimTimeNs{1001}, std::nullopt}}}, *graph, transport};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    Dispatcher dispatcher{transport, failures, [&](sim::SimulationContext& context) {
                              static_cast<void>(
                                  transport.submit_transfer({topology::NodeId{topology::NicId{0}},
                                                             topology::NodeId{topology::NicId{1}},
                                                             transport::ByteCount{2500},
                                                             transport::ByteCount{1000},
                                                             {link}},
                                                            context));
                          }};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed);
    const auto transfer = required(transport.transfer_snapshot(transport::TransferId{0}));
    equal(transfer.delivered.bytes, 1000U);
    equal(transfer.dropped_link_down.bytes, 1500U);
    equal(required(transfer.completion).outcome, transport::TransferOutcome::Failed);
    equal(required(transfer.completion).timestamp, sim::SimTimeNs{1025});
    equal(failures.report().timeline.size(), 3U);
    falsity(failures.report().incidents[0].recovered);
    equal(transport.find_service(link)->statistics(simulation.now()).busy_time.count(), 1001U);
}
TEST(FailureTest, CalendarRejectsOverlapAndOrdersRecoveryBeforeAdjacentOnset) {
    const auto graph = topology::generate_two_gpu_direct();
    const auto link = configuration(*graph).front().link.link;
    const Script script{{{link, sim::SimTimeNs{10}, sim::SimTimeNs{20}},
                         {link, sim::SimTimeNs{0}, sim::SimTimeNs{10}}}};
    const auto actions = validate(script, *graph);
    equal(actions.size(), 4U);
    equal(actions.at(0).incident, 1U);
    truth(actions.at(1).recovery);
    equal(actions.at(2).incident, 0U);
    auto invalid = script;
    invalid.incidents[1].recovery = sim::SimTimeNs{11};
    expect_throw<std::invalid_argument>([&] { static_cast<void>(validate(invalid, *graph)); });
    invalid = script;
    invalid.incidents[1].recovery.reset();
    expect_throw<std::invalid_argument>([&] { static_cast<void>(validate(invalid, *graph)); });
    expect_throw<std::length_error>(
        [&] { static_cast<void>(validate(script, *graph, Limits{1, 1, 1})); });
    expect_throw<std::invalid_argument>([&] {
        static_cast<void>(
            validate(Script{{{topology::LinkId{0}, sim::SimTimeNs{0}, std::nullopt}}}, *graph));
    });
    expect_throw<std::invalid_argument>([&] {
        static_cast<void>(
            validate(Script{{{link, sim::SimTimeNs{10}, sim::SimTimeNs{10}}}}, *graph));
    });
}
TEST(FailureTest, BoundsAndStoppedActionsRemainExplicit) {
    const auto graph = topology::generate_two_gpu_direct();
    const auto configs = configuration(*graph);
    const auto link = configs.front().link;
    transport::TransportRuntime transport{*graph, configs};
    Runtime failures{Script{{{link.link, sim::SimTimeNs{10}, sim::SimTimeNs{20}}}}, *graph,
                     transport};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    expect_throw<std::logic_error>([&] { failures.schedule(simulation); });
    Dispatcher dispatcher{transport, failures,
                          [](sim::SimulationContext& c) { c.stop(sim::StopReason::Requested); }};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{5}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Stopped);
    falsity(failures.report().incidents[0].applied);
    equal(failures.report().timeline.size(), 1U);
}
workload::TrainingScenario anchor() {
    workload::TrainingScenario result;
    result.gpus = 512;
    result.routing_policy = "shortest-path";
    result.bandwidth = transport::BitsPerSecond{8'000'000'000ULL};
    result.propagation = sim::SimDurationNs{25};
    const auto graph = topology::generate_clos({512, 8, 8, 8});
    routing::PathService paths{*graph};
    const auto& path =
        paths.lookup({topology::NodeId{topology::NicId{0}}, topology::NodeId{topology::NicId{8}}})
            .front();
    const auto target = path.at(1).link;
    const auto* link = graph->find(target);
    const auto a = graph->find(link->endpoint_a)->owner;
    const auto b = graph->find(link->endpoint_b)->owner;
    if (a.kind() != topology::NodeKind::Switch || b.kind() != topology::NodeKind::Switch ||
        graph->find(topology::SwitchId{a.value()})->role != topology::SwitchRole::Leaf ||
        graph->find(topology::SwitchId{b.value()})->role != topology::SwitchRole::Spine) {
        throw std::logic_error{"anchor target is not a leaf-spine link"};
    }
    result.failure_script = Script{{{target, sim::SimTimeNs{2500}, sim::SimTimeNs{30000}}}};
    for (std::uint64_t index = 0; index < 3; ++index) {
        workload::JobSpec job;
        job.name = "anchor";
        constexpr std::array<std::uint64_t, 3> arrivals{0, 5000, 40000};
        job.arrival = sim::SimTimeNs{arrivals.at(static_cast<std::size_t>(index))};
        job.workers = {topology::GpuId{index * 8}, topology::GpuId{64 + index * 8}};
        job.compute = {sim::SimDurationNs{1000}, sim::SimDurationNs{1000}};
        job.gradient_bytes = transport::ByteCount{4000};
        job.bucket_bytes = transport::ByteCount{4000};
        job.chunk_bytes = transport::ByteCount{1000};
        result.jobs.push_back(job);
    }
    return result;
}
TEST(FailureTest, SpineFailureAbortsOldJobReroutesNewWorkAndRestoresEligibility) {
    const auto scenario = anchor();
    const auto report = workload::run_training(scenario);
    truth(report.failures.has_value());
    equal(report.jobs.size(), 3U);
    equal(report.jobs.at(0).state, workload::JobState::Failed);
    equal(report.jobs.at(1).state, workload::JobState::Succeeded);
    equal(report.jobs.at(2).state, workload::JobState::Succeeded);
    const auto incident = required(report.failures).incidents.at(0);
    greater(incident.dropped.bytes, 0U);
    equal(incident.affected_jobs.size(), 1U);
    equal(incident.affected_jobs.at(0), workload::JobId{0});
    bool after_recovery_uses_target = false;
    for (const auto& decision : report.decisions) {
        const bool uses_target =
            std::any_of(decision.path.begin(), decision.path.end(),
                        [&](const auto& arc) { return arc.link == incident.specification.target; });
        if (decision.timestamp >= sim::SimTimeNs{2500} &&
            decision.timestamp < sim::SimTimeNs{30000}) {
            falsity(uses_target);
        }
        if (decision.timestamp >= sim::SimTimeNs{40000} && uses_target) {
            after_recovery_uses_target = true;
        }
    }
    truth(after_recovery_uses_target);
}
TEST(FailureTest, DomainResultsRepeatAcrossTelemetryModesAndKeepPolicyIndependentScriptIdentity) {
    auto scenario = anchor();
    const auto reference = workload::run_training(scenario);
    for (const auto mode : {telemetry::TelemetryMode::Off, telemetry::TelemetryMode::Summary,
                            telemetry::TelemetryMode::Sampled, telemetry::TelemetryMode::Full}) {
        scenario.telemetry.mode = mode;
        const auto repeated = workload::run_training(scenario);
        equal(repeated.failures, reference.failures);
        equal(workload::domain_outcome_digest(repeated),
              workload::domain_outcome_digest(reference));
    }
    for (const std::string policy : {"ecmp", "queue-aware"}) {
        scenario.routing_policy = policy;
        const auto compared = workload::run_training(scenario);
        equal(required(compared.failures).script_digest,
              required(reference.failures).script_digest);
    }
    const auto scripted = workload::domain_outcome_digest(reference);
    auto changed = reference;
    auto changed_failures = required(changed.failures);
    changed_failures.incidents.at(0).dropped.bytes += 1;
    changed.failures = changed_failures;
    different(scripted, workload::domain_outcome_digest(changed));
}
TEST(FailureTest, ScriptDigestMatchesIndependentLittleEndianGolden) {
    const auto graph = topology::generate_two_gpu_direct();
    const Script script{{{topology::LinkId{2}, sim::SimTimeNs{10}, sim::SimTimeNs{20}}}};
    // Independently encoded with Python struct.pack('<Q', value) and FNV-1a over canonical topology
    // YAML.
    equal(script_digest(script, *graph), 0x8179943a53468c39ULL);
    auto changed = script;
    changed.incidents[0].onset = sim::SimTimeNs{11};
    different(script_digest(changed, *graph), script_digest(script, *graph));
    static_assert(sizeof(sim::Event) <= 80);
}
TEST(FailureTest, AttributionExhaustionIsAnExplicitRunFailure) {
    const auto graph = topology::generate_two_gpu_direct();
    const auto configs = configuration(*graph);
    const auto link = configs.front().link;
    transport::TransportRuntime transport{*graph, configs};
    Runtime failures{Script{{{link.link, sim::SimTimeNs{10}, std::nullopt}}},
                     *graph,
                     transport,
                     nullptr,
                     nullptr,
                     Limits{10, 1, 1}};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    Dispatcher dispatcher{transport, failures, [&](sim::SimulationContext& context) {
                              static_cast<void>(
                                  transport.submit_transfer({topology::NodeId{topology::NicId{0}},
                                                             topology::NodeId{topology::NicId{1}},
                                                             transport::ByteCount{2500},
                                                             transport::ByteCount{1000},
                                                             {link}},
                                                            context));
                          }};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    const auto result = simulation.run(dispatcher);
    equal(result.status, sim::SimulationStatus::Failed);
    truth(result.error.has_value());
    different(required(result.error).find("attributed-chunk limit"), std::string::npos);
    equal(failures.report().drops.size(), 1U);
}
TEST(FailureTest, CheckedInSpineDemoRunsAndIncompleteJsonExportIsRejected) {
    const auto path = (std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path() /
                       "examples/training/spine-link-failure.yaml")
                          .string();
    std::ostringstream output;
    std::ostringstream error;
    const std::array<std::string_view, 4> arguments{"train", "--file", path, "--timeline"};
    equal(cli::run(arguments, output, error), 1);
    truth(error.str().empty());
    different(output.str().find("job=0 state=failed"), std::string::npos);
    different(output.str().find("job=1 state=succeeded"), std::string::npos);
    different(output.str().find("job=2 state=succeeded"), std::string::npos);
    different(output.str().find("transition=recovered time_ns=30000"), std::string::npos);
    const std::array<std::string_view, 4> json_arguments{"train", "--file", path,
                                                         "--telemetry-summary-json"};
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(cli::run(json_arguments, output, error)); });
}
TEST(FailureTest, StrictParsingRejectsFutureFeaturesAndDuplicateKeys) {
    const std::string prefix = "version: 1\njobs: [{workers: [0, 8]}]\nfailure_script: ";
    for (
        const std::string body :
        {"{version: 2, detection: immediate, incidents: []}",
         "{version: 1, detection: delayed, incidents: []}",
         "{version: 1, detection: immediate, delay_ns: 1, incidents: []}",
         "{version: 1, version: 1, detection: immediate, incidents: []}",
         "{version: 1, detection: immediate, incidents: [{kind: gpu_down, target: 0, at_ns: 0}]}",
         "{version: 1, detection: immediate, incidents: [], loads: [{}]}",
         R"({version: 1, detection: immediate, incidents: [{kind: link_down, target: -1, at_ns: 0}]})"}) {
        expect_throw<std::invalid_argument>(
            [&] { static_cast<void>(workload::parse_scenario(prefix + body)); });
    }
    auto scenario =
        workload::parse_scenario(prefix + "{version: 1, detection: immediate, incidents: [{kind: "
                                          "link_down, target: 99999, at_ns: 0}]}");
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(workload::run_training(scenario)); });
    scenario.failure_script.reset();
    falsity(workload::run_training(scenario).failures.has_value());
}
} // namespace
} // namespace nexuslab::failures
