// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/cli/application.hpp"
#include "nexuslab/failures/runtime.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/workload/digest.hpp"
#include "nexuslab/workload/dispatcher.hpp"
#include <array>
#include <filesystem>
#include <fstream>
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
    void operator()(const transport::PortStateChangeEvent& e, sim::SimulationContext& c) {
        failures.handle(e, c);
    }
    void operator()(const transport::SwitchStateChangeEvent& e, sim::SimulationContext& c) {
        failures.handle(e, c);
    }
    void operator()(const transport::LinkCapacityChangeEvent& e, sim::SimulationContext& c) {
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
            std::any_of(decision.path.begin(), decision.path.end(), [&](const auto& arc) {
                return arc.link.value() == target_value(incident.specification.target);
            });
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
TEST(FailureTest, CheckedInSpineDemoRunsAndCatalogV2ExportPreservesFailure) {
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
    output.str("");
    equal(cli::run(json_arguments, output, error), 1);
    different(output.str().find("\"catalog_version\":2"), std::string::npos);
    different(output.str().find("\"domain_success\":false"), std::string::npos);
}
TEST(FailureTest, StrictParsingRejectsFutureFeaturesAndDuplicateKeys) {
    const std::string prefix = "version: 1\njobs: [{workers: [0, 8]}]\nfailure_script: ";
    for (
        const std::string body :
        {"{version: 2, detection: immediate, incidents: []}",
         "{version: 1, detection: delayed, incidents: []}",
         "{version: 1, detection: immediate, delay_ns: 1, incidents: []}",
         "{version: 1, version: 1, detection: immediate, incidents: []}",
         "{version: 1, detection: immediate, incidents: [{kind: nic_down, target: 0, at_ns: 0}]}",
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
TEST(FailureTest, CapacityChangePreservesActiveTimingAndRestoresDirectionalBaselines) {
    const auto graph = topology::generate_two_gpu_direct();
    auto configs = configuration(*graph);
    configs.at(1).bandwidth = transport::BitsPerSecond{16'000'000'000ULL};
    const auto link = configs.front().link;
    transport::TransportRuntime transport{*graph, configs};
    Runtime failures{Script{{{link.link, sim::SimTimeNs{500}, sim::SimTimeNs{1500},
                              Kind::LinkDegraded, transport::BitsPerSecond{4'000'000'000ULL}}}},
                     *graph, transport};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    Dispatcher dispatcher{
        transport, failures, [&](sim::SimulationContext& context) {
            if (context.now().count() == 0) {
                static_cast<void>(transport.submit_transfer({topology::NodeId{topology::NicId{0}},
                                                             topology::NodeId{topology::NicId{1}},
                                                             transport::ByteCount{2500},
                                                             transport::ByteCount{1000},
                                                             {link}},
                                                            context));
            } else {
                const auto* service = transport.find_service(link);
                const bool degraded = context.now().count() == 750;
                equal(service->queue().configuration().bandwidth.value(),
                      degraded ? 4'000'000'000ULL : 8'000'000'000ULL);
                equal(required(service->active_bandwidth()).value(),
                      degraded ? 8'000'000'000ULL : 4'000'000'000ULL);
                equal(routing::FabricView{transport}.read(link).bandwidth,
                      service->queue().configuration().bandwidth);
            }
        }};
    for (const auto time : {0U, 750U, 1750U}) {
        static_cast<void>(simulation.schedule(
            {sim::SimTimeNs{time}, sim::EventPriority::Normal, sim::NoOpEvent{time}}));
    }
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed);
    const auto transfer = required(transport.transfer_snapshot(transport::TransferId{0}));
    equal(required(transfer.completion).timestamp, sim::SimTimeNs{3525});
    equal(transfer.delivered.bytes, 2500U);
    equal(transfer.dropped_link_down.bytes, 0U);
    equal(transport.find_service(configs.at(1).link)->queue().configuration().bandwidth,
          configs.at(1).bandwidth);
    equal(transport.capacity_revision(), 2U);
    equal(graph->operational_revision(), 0U);
    equal(failures.report().incidents.at(0).dropped.bytes, 0U);
    equal(failures.report().outcome_version, 3U);
    equal(failures.report().timeline.at(1).capacity_revision, 1U);
    equal(failures.report().timeline.back().capacity_revision, 2U);
    equal(sim::payload_kind(transport::LinkCapacityChangeEvent{link.link, configs.at(0).bandwidth,
                                                               configs.at(1).bandwidth}),
          sim::EventPayloadKind::LinkCapacityChange);
    static_assert(sizeof(sim::Event) == 72);
}
TEST(FailureTest, PortSwitchAndLinkRestorationsPreserveOtherDownCausesAndUniqueDrops) {
    const auto graph = topology::generate_single_rack({2, 1});
    const auto configs = configuration(*graph);
    const auto link = configs.front().link;
    const auto port = graph->find(link.link)->endpoint_b;
    const auto sw = topology::SwitchId{graph->find(port)->owner.value()};
    const routing::Path route{configs.at(0).link, configs.at(3).link};
    transport::TransportRuntime transport{*graph, configs};
    const Script script{{{link.link, sim::SimTimeNs{100}, sim::SimTimeNs{200}},
                         {port, sim::SimTimeNs{50}, sim::SimTimeNs{250}, Kind::PortDown},
                         {sw, sim::SimTimeNs{75}, sim::SimTimeNs{300}, Kind::SwitchDown}}};
    Runtime failures{script, *graph, transport};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    Dispatcher dispatcher{
        transport, failures, [&](sim::SimulationContext& context) {
            const auto now = context.now().count();
            if (now == 210) {
                truth(failures.report().incidents.at(0).recovered);
                falsity(failures.report().incidents.at(0).effective_operational);
                return;
            }
            if (now == 260) {
                truth(failures.report().incidents.at(1).recovered);
                falsity(failures.report().incidents.at(1).effective_operational);
                return;
            }
            static_cast<void>(transport.submit_transfer(
                {topology::NodeId{topology::NicId{0}}, topology::NodeId{topology::NicId{1}},
                 transport::ByteCount{now == 0 ? 2500U : 1000U}, transport::ByteCount{1000}, route},
                context));
        }};
    for (const auto time : {0U, 150U, 210U, 260U, 300U}) {
        static_cast<void>(simulation.schedule(
            {sim::SimTimeNs{time}, sim::EventPriority::Normal, sim::NoOpEvent{time}}));
    }
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed);
    const auto report = failures.report();
    equal(report.incidents.at(0).dropped.bytes, 1000U);
    equal(report.incidents.at(1).dropped.bytes, 2500U);
    equal(report.incidents.at(2).dropped.bytes, 0U);
    equal(report.drops.size(), 4U);
    truth(report.incidents.at(0).effective_operational);
    truth(report.incidents.at(1).effective_operational);
    const auto restored = required(transport.transfer_snapshot(transport::TransferId{2}));
    equal(required(restored.completion).outcome, transport::TransferOutcome::Succeeded);
}
TEST(FailureTest, QueueAwareRoutingReadsChangedCapacityWithoutInvalidatingReachabilityCache) {
    const auto graph = topology::generate_leaf_spine({2, 1, 1, 2});
    const auto configs = configuration(*graph);
    transport::TransportRuntime transport{*graph, configs};
    routing::PathService paths{*graph};
    const routing::Endpoints endpoints{topology::NodeId{topology::NicId{0}},
                                       topology::NodeId{topology::NicId{1}}};
    const auto link = paths.lookup(endpoints).front().at(1).link;
    Runtime failures{Script{{{link, sim::SimTimeNs{500}, sim::SimTimeNs{1500}, Kind::LinkDegraded,
                              transport::BitsPerSecond{4'000'000'000ULL}}}},
                     *graph, transport};
    routing::Router router{*graph, transport, routing::PolicyRegistry{}, {"queue-aware", 42}};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    Dispatcher dispatcher{transport, failures, [&](sim::SimulationContext& context) {
                              static_cast<void>(router.submit({context.now().count(), endpoints,
                                                               transport::ByteCount{1000},
                                                               transport::ByteCount{1000}},
                                                              context));
                          }};
    for (const auto time : {0U, 600U}) {
        static_cast<void>(simulation.schedule(
            {sim::SimTimeNs{time}, sim::EventPriority::Normal, sim::NoOpEvent{time}}));
    }
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed);
    const auto decisions = router.decisions();
    equal(decisions.size(), 2U);
    equal(decisions[0].capacity_revision, 0U);
    equal(decisions[1].capacity_revision, 1U);
    different(decisions[0].path, decisions[1].path);
    equal(decisions[0].operational_revision, decisions[1].operational_revision);
    equal(router.cache_statistics().invalidations, 0U);
}
workload::TrainingScenario gpu_scenario() {
    auto result = anchor();
    result.gpus = 64;
    result.scheduling = scheduling::Configuration{};
    result.jobs.resize(2);
    result.jobs.at(0).workers = {topology::GpuId{0}, topology::GpuId{8}};
    result.jobs.at(1).workers = {topology::GpuId{0}, topology::GpuId{16}};
    result.jobs.at(1).arrival = sim::SimTimeNs{3000};
    result.failure_script =
        Script{{{topology::GpuId{0}, sim::SimTimeNs{2500}, sim::SimTimeNs{10000}, Kind::GpuDown}}};
    return result;
}
TEST(FailureTest, GpuFailureAbortsOwnerDrainsIssuedTrafficAndReadmitsOnlyAfterRestoration) {
    const auto report = workload::run_training(gpu_scenario());
    equal(report.jobs.size(), 2U);
    equal(report.jobs.at(0).state, workload::JobState::Failed);
    equal(report.jobs.at(0).elapsed_ns, 2500U);
    equal(report.jobs.at(1).state, workload::JobState::Succeeded);
    equal(report.jobs.at(1).waiting_ns, 7000U);
    const auto failures = required(report.failures);
    equal(failures.incidents.at(0).affected_jobs, std::vector{workload::JobId{0}});
    equal(failures.incidents.at(0).dropped.bytes, 0U);
    equal(report.collectives.at(0).outcome, collective::Phase::Cancelled);
    greater(report.collectives.at(0).delivered_bytes, 0U);
    equal(workload::resolved_domain_outcome_digest_version(report), 3U);
}
TEST(FailureTest, GpuConflictsAndInvalidCapacityAreRejectedBeforeScheduling) {
    auto scenario = gpu_scenario();
    scenario.scheduling.reset();
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(workload::run_training(scenario)); });
    scenario = gpu_scenario();
    scenario.gpu_controls.push_back({topology::GpuId{0}, false, sim::SimTimeNs{100}});
    expect_throw<std::invalid_argument>(
        [&] { static_cast<void>(workload::run_training(scenario)); });
    const auto graph = topology::generate_two_gpu_direct();
    const auto configs = configuration(*graph);
    transport::TransportRuntime transport{*graph, configs};
    for (const auto capacity : {0ULL, 8'000'000'000ULL, 9'000'000'000ULL}) {
        expect_throw<std::invalid_argument>([&] {
            Runtime invalid{
                Script{{{configs.front().link.link, sim::SimTimeNs{1}, sim::SimTimeNs{2},
                         Kind::LinkDegraded, transport::BitsPerSecond{capacity}}}},
                *graph, transport};
        });
    }
    const Script overlapping{{{configs.front().link.link, sim::SimTimeNs{0}, sim::SimTimeNs{10}},
                              {configs.front().link.link, sim::SimTimeNs{5}, sim::SimTimeNs{20},
                               Kind::LinkDegraded, transport::BitsPerSecond{4'000'000'000ULL}}}};
    expect_throw<std::invalid_argument>([&] { static_cast<void>(validate(overlapping, *graph)); });
    const Script mismatched{
        {{topology::PortId{0}, sim::SimTimeNs{0}, std::nullopt, Kind::SwitchDown}}};
    expect_throw<std::invalid_argument>([&] { static_cast<void>(validate(mismatched, *graph)); });
    const Script local_port{
        {{topology::PortId{0}, sim::SimTimeNs{0}, std::nullopt, Kind::PortDown}}};
    expect_throw<std::invalid_argument>([&] { static_cast<void>(validate(local_port, *graph)); });
}
TEST(FailureTest, SwitchFailureAndNewKindsRemainDeterministicAcrossTelemetryModes) {
    auto scenario = anchor();
    scenario.gpus = 64;
    scenario.jobs.resize(2);
    scenario.jobs.at(0).workers = {topology::GpuId{0}, topology::GpuId{8}};
    scenario.jobs.at(1).workers = {topology::GpuId{16}, topology::GpuId{24}};
    scenario.jobs.at(1).arrival = sim::SimTimeNs{12000};
    scenario.failure_script = Script{
        {{topology::SwitchId{0}, sim::SimTimeNs{2500}, sim::SimTimeNs{10000}, Kind::SwitchDown}}};
    const auto reference = workload::run_training(scenario);
    equal(reference.jobs.at(0).state, workload::JobState::Failed);
    equal(reference.jobs.at(1).state, workload::JobState::Succeeded);
    equal(required(reference.failures).incidents.at(0).affected_jobs,
          std::vector{workload::JobId{0}});
    for (const auto mode : {telemetry::TelemetryMode::Off, telemetry::TelemetryMode::Summary,
                            telemetry::TelemetryMode::Sampled, telemetry::TelemetryMode::Full}) {
        scenario.telemetry.mode = mode;
        const auto repeated = workload::run_training(scenario);
        equal(repeated.failures, reference.failures);
        equal(workload::domain_outcome_digest(repeated),
              workload::domain_outcome_digest(reference));
    }
    auto mutated = reference;
    mutated.decisions.at(0).capacity_revision = 1;
    different(workload::domain_outcome_digest(mutated), workload::domain_outcome_digest(reference));
}
TEST(FailureTest, ResourceExamplesParseExecuteAndRepeatAcrossModes) {
    const auto directory =
        std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path() /
        "examples/training";
    for (const std::string name :
         {"resource-recovery.yaml", "gpu-recovery.yaml", "link-degradation.yaml"}) {
        std::ifstream stream{directory / name};
        const std::string yaml{std::istreambuf_iterator<char>{stream},
                               std::istreambuf_iterator<char>{}};
        auto scenario = workload::parse_scenario(yaml);
        const auto reference = workload::run_training(scenario);
        equal(workload::resolved_domain_outcome_digest_version(reference), 3U);
        const bool degradation = name == "link-degradation.yaml";
        equal(reference.jobs.at(0).state,
              degradation ? workload::JobState::Succeeded : workload::JobState::Failed);
        equal(reference.jobs.at(1).state, workload::JobState::Succeeded);
        for (const auto mode :
             {telemetry::TelemetryMode::Off, telemetry::TelemetryMode::Summary,
              telemetry::TelemetryMode::Sampled, telemetry::TelemetryMode::Full}) {
            scenario.telemetry.mode = mode;
            const auto repeated = workload::run_training(scenario);
            equal(repeated.failures, reference.failures);
            equal(workload::domain_outcome_digest(repeated),
                  workload::domain_outcome_digest(reference));
        }
        if (degradation) {
            equal(required(reference.failures).capacity_revision, 2U);
            truth(required(reference.failures).drops.empty());
        }
    }
}
TEST(FailureTest, GpuFailureAtTimeZeroLeavesBlockedJobExplicitlyUnfinished) {
    auto scenario = gpu_scenario();
    scenario.jobs.resize(1);
    scenario.failure_script =
        Script{{{topology::GpuId{0}, sim::SimTimeNs{0}, std::nullopt, Kind::GpuDown}}};
    const auto report = workload::run_training(scenario);
    equal(report.simulation.status, sim::SimulationStatus::Completed);
    equal(report.jobs.at(0).state, workload::JobState::Waiting);
    const auto incident = required(report.failures).incidents.at(0);
    truth(incident.applied);
    falsity(incident.recovered);
    falsity(incident.effective_operational);
}
TEST(FailureTest, ReservedCapacityRejectsOverflowBeforeAllocatingTransferIds) {
    const auto graph = topology::generate_two_gpu_direct();
    const auto configs = configuration(*graph);
    const auto link = configs.front().link;
    transport::TransportRuntime transport{*graph, configs};
    Runtime failures{Script{{{link.link, sim::SimTimeNs{1}, sim::SimTimeNs{2}, Kind::LinkDegraded,
                              transport::BitsPerSecond{1}}}},
                     *graph, transport};
    sim::Simulation simulation{42};
    failures.schedule(simulation);
    Dispatcher dispatcher{
        transport, failures, [&](sim::SimulationContext& context) {
            const transport::TransferRequest invalid{topology::NodeId{topology::NicId{0}},
                                                     topology::NodeId{topology::NicId{1}},
                                                     transport::ByteCount{3'000'000'000ULL},
                                                     transport::ByteCount{3'000'000'000ULL},
                                                     {link}};
            expect_throw<std::overflow_error>(
                [&] { static_cast<void>(transport.submit_transfer(invalid, context)); });
            auto valid = invalid;
            valid.bytes = transport::ByteCount{1};
            valid.maximum_chunk_bytes = transport::ByteCount{1};
            equal(transport.submit_transfer(valid, context).id, transport::TransferId{0});
        }};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    equal(simulation.run(dispatcher).status, sim::SimulationStatus::Completed);
}

workload::TrainingScenario no_route_scenario() {
    return workload::parse_scenario(R"(version: 1
gpus: 64
routing_policy: shortest-path
bandwidth_bps: 8000000000
propagation_ns: 25
jobs:
  - {name: blocked-admission, workers: [0, 8], compute_ns: 1000, gradient_bytes: 4000, bucket_bytes: 4000, chunk_bytes: 1000}
)");
}
topology::DirectedLink source_fabric(const topology::TopologyGraph& graph) {
    for (const auto& arc : graph.outgoing(topology::NodeId{topology::NicId{0}})) {
        if (graph.find(arc.id.link)->kind == topology::LinkKind::Fabric) {
            return arc;
        }
    }
    throw std::logic_error{"missing source fabric arc"};
}
TEST(FailureTest, UnroutableJobsAttributeLinkPortAndSwitchWithoutFabricatedDrops) {
    const auto graph = topology::generate_clos({64, 8, 8, 8});
    const auto arc = source_fabric(*graph);
    const auto sw = topology::SwitchId{graph->find(arc.destination)->owner.value()};
    for (const auto& incident : std::vector<Incident>{
             {arc.id.link, sim::SimTimeNs{0}, sim::SimTimeNs{5000}},
             {arc.source, sim::SimTimeNs{0}, sim::SimTimeNs{5000}, Kind::PortDown},
             {sw, sim::SimTimeNs{0}, sim::SimTimeNs{5000}, Kind::SwitchDown}}) {
        auto scenario = no_route_scenario();
        scenario.failure_script = Script{{incident}};
        auto later = scenario.jobs.front();
        later.arrival = sim::SimTimeNs{6000};
        scenario.jobs.push_back(later);
        const auto report = workload::run_training(scenario);
        equal(report.jobs.front().state, workload::JobState::Failed);
        equal(report.jobs.back().state, workload::JobState::Succeeded);
        const auto failures = required(report.failures);
        equal(failures.incidents.front().affected_jobs, std::vector{workload::JobId{0}});
        equal(failures.incidents.front().dropped.bytes, 0U);
        equal(failures.drops.size(), 0U);
        equal(report.decisions.front().transfer.has_value(), false);
        equal(report.decisions.front().candidates, 0U);
    }
}
TEST(FailureTest, UnroutableAttributionUsesDeclarationOrderAndCurrentRecoveryState) {
    const auto graph = topology::generate_clos({64, 8, 8, 8});
    const auto arc = source_fabric(*graph);
    auto scenario = no_route_scenario();
    scenario.failure_script = Script{
        {{topology::SwitchId{0}, sim::SimTimeNs{500}, sim::SimTimeNs{2000}, Kind::SwitchDown},
         {arc.id.link, sim::SimTimeNs{0}, sim::SimTimeNs{5000}}}};
    for (const auto arrival : {2500U, 6000U}) {
        auto job = scenario.jobs.front();
        job.arrival = sim::SimTimeNs{arrival};
        scenario.jobs.push_back(job);
    }
    const auto report = workload::run_training(scenario);
    const auto failures = required(report.failures);
    equal(failures.incidents.at(0).affected_jobs, std::vector{workload::JobId{0}});
    equal(failures.incidents.at(1).affected_jobs, std::vector{workload::JobId{1}});
    equal(report.jobs.at(2).state, workload::JobState::Succeeded);
    equal(failures.drops.size(), 0U);
}
TEST(FailureTest, UnroutableAttributionExcludesNonminimumPathsAndCapacityImpairments) {
    const auto graph = topology::generate_clos({64, 8, 8, 8});
    const auto arc = source_fabric(*graph);
    auto scenario = no_route_scenario();
    scenario.failure_script =
        Script{{{topology::SwitchId{1}, sim::SimTimeNs{0}, std::nullopt, Kind::SwitchDown},
                {arc.id.link, sim::SimTimeNs{0}, std::nullopt, Kind::LinkDegraded,
                 transport::BitsPerSecond{4'000'000'000ULL}},
                {topology::SwitchId{0}, sim::SimTimeNs{0}, std::nullopt, Kind::SwitchDown}}};
    const auto report = workload::run_training(scenario);
    const auto failures = required(report.failures);
    equal(failures.incidents.at(0).affected_jobs.size(), 0U);
    equal(failures.incidents.at(1).affected_jobs.size(), 0U);
    equal(failures.incidents.at(2).affected_jobs, std::vector{workload::JobId{0}});
    equal(failures.drops.size(), 0U);
}
struct NoRouteFixture final {
    std::unique_ptr<topology::TopologyGraph> graph;
    transport::TransportRuntime transport;
    routing::Router router;
    collective::RingExecutor ring;
    workload::WorkloadEngine jobs;
    Runtime failures;
    workload::TrainingDispatcher dispatcher;
    sim::Simulation simulation{42};
    NoRouteFixture(std::unique_ptr<topology::TopologyGraph> topology, const Script& script,
                   Limits limits = {})
        : graph{std::move(topology)}, transport{*graph, configuration(*graph)},
          router{*graph, transport, routing::PolicyRegistry{}}, ring{*graph, router},
          jobs{*graph, ring}, failures{script, *graph, transport, &ring, &jobs, limits},
          dispatcher{jobs, ring, transport, &failures} {
        failures.schedule(simulation);
    }
    void schedule(std::size_t count = 1) {
        auto job = no_route_scenario().jobs.front();
        job.workers = {topology::GpuId{0}, topology::GpuId{1}};
        for (std::size_t i = 0; i < count; ++i) {
            job.arrival = sim::SimTimeNs{i * 2000};
            static_cast<void>(jobs.schedule(job, simulation));
        }
    }
};
TEST(FailureTest, UnscriptedDisconnectionHasNoIncidentAttribution) {
    NoRouteFixture fixture{topology::generate_two_gpu_direct(), Script{}};
    truth(fixture.graph->set_link_state(topology::LinkId{2}, topology::OperationalState::Down));
    fixture.schedule();
    equal(fixture.simulation.run(fixture.dispatcher).status, sim::SimulationStatus::Completed);
    equal(fixture.jobs.take_completed().front().state, workload::JobState::Failed);
    equal(fixture.failures.report().incidents.size(), 0U);
    equal(fixture.ring.take_no_route_admissions().size(), 0U);
}
TEST(FailureTest, UnroutableAttributionExhaustionFailsExplicitly) {
    NoRouteFixture fixture{topology::generate_two_gpu_direct(),
                           Script{{{topology::LinkId{2}, sim::SimTimeNs{0}, std::nullopt}}},
                           {10000, 1000000, 1}};
    fixture.schedule(2);
    const auto result = fixture.simulation.run(fixture.dispatcher);
    equal(result.status, sim::SimulationStatus::Failed);
    truth(required(result.error).find("affected-job limit") != std::string::npos);
    equal(fixture.failures.report().incidents.front().affected_jobs.size(), 1U);
    equal(fixture.failures.report().drops.size(), 0U);
}
TEST(FailureTest, HealthyMinimumPathAttributionIsNotTruncatedToRoutingCandidateLimit) {
    auto graph = topology::generate_leaf_spine({2, 1, 1, 70});
    const auto arc = source_fabric(*graph);
    const auto last_spine = graph->switches().back().id;
    NoRouteFixture fixture{std::move(graph),
                           Script{{{last_spine, sim::SimTimeNs{0}, std::nullopt, Kind::SwitchDown},
                                   {arc.id.link, sim::SimTimeNs{0}, std::nullopt}}}};
    fixture.schedule();
    equal(fixture.simulation.run(fixture.dispatcher).status, sim::SimulationStatus::Completed);
    const auto report = fixture.failures.report();
    equal(report.incidents.at(0).affected_jobs, std::vector{workload::JobId{0}});
    equal(report.incidents.at(1).affected_jobs.size(), 0U);
    equal(report.drops.size(), 0U);
}
TEST(FailureTest, UnroutableAttributionRepeatsAcrossModesAndDoesNotInventLoadJobs) {
    auto scenario = no_route_scenario();
    scenario.failure_script = Script{
        {{topology::SwitchId{0}, sim::SimTimeNs{0}, std::nullopt, Kind::SwitchDown}},
        {{traffic::Kind::Burst, topology::GpuId{16}, topology::GpuId{24}, sim::SimTimeNs{1000}, 1,
          sim::SimDurationNs{0}, transport::ByteCount{1000}, transport::ByteCount{1000}}}};
    const auto reference = workload::run_training(scenario);
    equal(required(reference.failures).incidents.front().affected_jobs,
          std::vector{workload::JobId{0}});
    equal(reference.loads.front().outcome, traffic::Outcome::NoRoute);
    equal(workload::resolved_domain_outcome_digest_version(reference), 4U);
    for (const auto mode : {telemetry::TelemetryMode::Off, telemetry::TelemetryMode::Summary,
                            telemetry::TelemetryMode::Sampled, telemetry::TelemetryMode::Full}) {
        scenario.telemetry.mode = mode;
        const auto report = workload::run_training(scenario);
        equal(report.failures, reference.failures);
        equal(workload::domain_outcome_digest(report), workload::domain_outcome_digest(reference));
    }
}
TEST(FailureTest, CheckedInNoRouteRecoveryDemoPreservesNegativeCliOutcome) {
    const auto path = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path() /
                      "examples/training/no-route-recovery.yaml";
    const auto filename = path.string();
    const std::array<std::string_view, 4> arguments{"train", "--file", filename, "--timeline"};
    std::ostringstream output;
    std::ostringstream error;
    equal(cli::run(arguments, output, error), 1);
    truth(output.str().find("job=0 state=failed") != std::string::npos);
    truth(output.str().find("job=1 state=succeeded") != std::string::npos);
    truth(output.str().find("dropped_bytes=0 dropped_chunks=0 affected_jobs=1") !=
          std::string::npos);
    truth(output.str().find("outcome=no_route") != std::string::npos);
    truth(output.str().find("outcome=succeeded") != std::string::npos);
    equal(error.str(), std::string{});
}
} // namespace
} // namespace nexuslab::failures
