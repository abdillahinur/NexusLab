// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/cli/application.hpp"
#include "nexuslab/telemetry/serialization.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/families.hpp"
#include "nexuslab/traffic/runtime.hpp"
#include "nexuslab/workload/digest.hpp"
#include "support/noop_dispatcher.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iterator>
#include <map>
#include <source_location>
#include <sstream>
#include <yaml-cpp/yaml.h>
namespace nexuslab::telemetry {
namespace {
template <typename A, typename B>
void equal(const A& a, const B& b, std::source_location where = std::source_location::current()) {
    SCOPED_TRACE(where.line());
    EXPECT_EQ(a, b);
}
void truth(bool value, std::source_location where = std::source_location::current()) {
    SCOPED_TRACE(where.line());
    EXPECT_TRUE(value);
}
template <typename T> T required(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::logic_error{"missing test value"};
    }
    return *value;
}
template <typename Exception, typename Operation> void rejects(Operation operation) {
    bool caught{false};
    try {
        operation();
    } catch (const Exception&) {
        caught = true;
    } catch (...) {
        caught = false;
    }
    truth(caught);
}
std::filesystem::path example(std::string_view name) {
    return std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path() /
           "examples/training" / name;
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file{path};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
TelemetryRunMetadata metadata(const workload::TrainingScenario& scenario,
                              const workload::TrainingReport& report) {
    TelemetryRunMetadata result;
    result.seed = scenario.seed;
    result.routing_policy = scenario.routing_policy;
    result.final_time = report.simulation.final_time;
    const auto failures = required(report.failures);
    result.script = ScriptMetadata{failures.script_digest, workload::domain_outcome_digest(report),
                                   workload::resolved_domain_outcome_digest_version(report),
                                   failures.capacity_revision, false};
    return result;
}
MetricSeriesSnapshot metric(const TelemetrySnapshot& snapshot, MetricId id,
                            const MetricLabels& labels) {
    for (const auto& series : snapshot.metrics) {
        if (series.metric == id && series.labels == labels) {
            return series;
        }
    }
    throw std::logic_error{"missing metric"};
}
bool has_metric(const TelemetrySnapshot& snapshot, MetricId id) {
    return std::any_of(snapshot.metrics.begin(), snapshot.metrics.end(),
                       [&](const auto& item) { return item.metric == id; });
}
void check_failure_state(const workload::TrainingReport& report) {
    const auto failures = required(report.failures);
    for (std::size_t i = 0; i < failures.incidents.size(); ++i) {
        const auto& state =
            std::get<FailureObservation>(report.telemetry.perturbations.at(i).observation);
        const auto details = required(state.details);
        const auto& expected = failures.incidents.at(i);
        equal(state.failure, expected.id);
        equal(details.applied, expected.applied);
        equal(details.recovered, expected.recovered);
        equal(details.effective_operational, expected.effective_operational);
        equal(details.capacity_revision, expected.capacity_revision);
        equal(details.affected_jobs, expected.affected_jobs.size());
        equal(details.dropped_bytes, expected.dropped.bytes);
        equal(details.dropped_chunks, expected.dropped.chunks);
    }
}
void check_load_state(const workload::TrainingReport& report) {
    const auto offset = required(report.failures).incidents.size();
    equal(report.telemetry.perturbations.size(), offset + report.loads.size());
    for (std::size_t i = 0; i < report.loads.size(); ++i) {
        const auto& state =
            std::get<LoadObservation>(report.telemetry.perturbations.at(offset + i).observation);
        const auto& request = report.loads.at(i);
        equal(state.source, request.source.value());
        equal(state.ordinal, request.ordinal);
        equal(state.injected, request.injected);
        equal(state.finished, request.finished);
        equal(state.transfer, request.transfer);
        equal(state.outcome, static_cast<std::uint64_t>(request.outcome));
        equal(state.delivered.value(), request.delivered.bytes);
        equal(state.buffer_dropped.value(), request.dropped_buffer_full.bytes);
        equal(state.down_dropped.value(), request.dropped_link_down.bytes);
    }
}
TEST(FailureTelemetryTest, FullTraceRebuildsCanonicalSummaryForAllPerturbationKinds) {
    for (const std::string name :
         {"spine-link-failure.yaml", "resource-recovery.yaml", "gpu-recovery.yaml",
          "link-degradation.yaml", "background-loads.yaml", "background-load-loss.yaml",
          "no-route-recovery.yaml"}) {
        auto scenario = workload::parse_scenario(read(example(name)));
        scenario.telemetry.mode = TelemetryMode::Full;
        const auto report = workload::run_training(scenario);
        const auto provenance = metadata(scenario, report);
        auto trace = report.telemetry;
        trace.metrics.clear();
        trace.job_attributions.clear();
        trace.perturbations.clear();
        const auto rebuilt = reconstruct_full_trace(trace, provenance, 2);
        equal(rebuilt.metrics, report.telemetry.metrics);
        equal(rebuilt.job_attributions, report.telemetry.job_attributions);
        equal(rebuilt.perturbations, report.telemetry.perturbations);
        equal(serialize_summary_json(rebuilt, provenance),
              serialize_summary_json(report.telemetry, provenance));
        check_failure_state(report);
        check_load_state(report);
    }
}
TEST(FailureTelemetryTest, LifecycleAndNoRouteMetricsHaveExactMeaning) {
    const auto scenario = workload::parse_scenario(read(example("no-route-recovery.yaml")));
    const auto report = workload::run_training(scenario);
    const MetricLabels labels{{MetricLabel::Failure, 1}, {MetricLabel::Resource, 3}};
    equal(metric(report.telemetry, MetricId::FailureStateTransitionTotal, labels).scalar, 5U);
    equal(metric(report.telemetry, MetricId::FailureAffectedJobs, labels).scalar, 1U);
    truth(!has_metric(report.telemetry, MetricId::FailureAffectedTrafficBytes));
    const auto detected =
        metric(report.telemetry, MetricId::FailureDetectionTimeNs, {{MetricLabel::Resource, 3}});
    equal(detected.histogram_count, 1U);
    equal(detected.histogram_sum, 0U);
    const auto recovery =
        metric(report.telemetry, MetricId::FailureRecoveryTimeNs, {{MetricLabel::Resource, 3}});
    equal(recovery.histogram_count, 1U);
    equal(recovery.histogram_sum, 5000U);
}
TEST(FailureTelemetryTest, DropMetricsMatchUniqueDomainBytesAndJobs) {
    const auto scenario = workload::parse_scenario(read(example("resource-recovery.yaml")));
    const auto report = workload::run_training(scenario);
    for (const auto& incident : required(report.failures).incidents) {
        const MetricLabels labels{
            {MetricLabel::Failure, incident.id.value()},
            {MetricLabel::Resource, incident.specification.target.index() + 1}};
        equal(metric(report.telemetry, MetricId::FailureAffectedJobs, labels).scalar,
              incident.affected_jobs.size());
        equal(metric(report.telemetry, MetricId::FailureAffectedTrafficBytes, labels).scalar,
              incident.dropped.bytes);
        equal(metric(report.telemetry, MetricId::FailureStateTransitionTotal, labels).scalar, 5U);
    }
}
TEST(FailureTelemetryTest, PermanentFailureHasNoInventedRecoverySample) {
    auto scenario = workload::parse_scenario(read(example("no-route-recovery.yaml")));
    auto script = required(scenario.failure_script);
    script.incidents.front().recovery.reset();
    scenario.failure_script = std::move(script);
    const auto report = workload::run_training(scenario);
    truth(!has_metric(report.telemetry, MetricId::FailureRecoveryTimeNs));
    equal(metric(report.telemetry, MetricId::FailureStateTransitionTotal,
                 {{MetricLabel::Failure, 1}, {MetricLabel::Resource, 3}})
              .scalar,
          3U);
    equal(metric(report.telemetry, MetricId::FailureAffectedJobs,
                 {{MetricLabel::Failure, 1}, {MetricLabel::Resource, 3}})
              .scalar,
          2U);
    check_failure_state(report);
}
TEST(FailureTelemetryTest, ModesPreserveDomainAndObservedSummaryState) {
    for (const std::string name :
         {"resource-recovery.yaml", "link-degradation.yaml", "no-route-recovery.yaml"}) {
        auto scenario = workload::parse_scenario(read(example(name)));
        const auto reference = workload::run_training(scenario);
        for (const auto mode : {TelemetryMode::Off, TelemetryMode::Summary, TelemetryMode::Sampled,
                                TelemetryMode::Full}) {
            scenario.telemetry.mode = mode;
            const auto report = workload::run_training(scenario);
            equal(workload::domain_outcome_digest(report),
                  workload::domain_outcome_digest(reference));
            if (mode == TelemetryMode::Off) {
                equal(report.telemetry.perturbations.size(), 0U);
            } else {
                equal(report.telemetry.perturbations, reference.telemetry.perturbations);
                equal(report.telemetry.metrics, reference.telemetry.metrics);
            }
        }
    }
}
TEST(FailureTelemetryTest, CapacitySnapshotsAndRoutingRevisionsAreExported) {
    auto scenario = workload::parse_scenario(read(example("link-degradation.yaml")));
    scenario.telemetry.mode = TelemetryMode::Full;
    const auto report = workload::run_training(scenario);
    bool degraded = false;
    bool restored = false;
    for (const auto& record : report.telemetry.records) {
        if (const auto* failure = std::get_if<FailureObservation>(&record.observation)) {
            const auto details = required(failure->details);
            const auto baseline = required(details.baseline_capacity_bps);
            const auto effective = required(details.effective_capacity_bps);
            if (failure->transition == FailureTransition::Applied) {
                truth(effective.at(0) < baseline.at(0));
                degraded = true;
            }
            if (failure->transition == FailureTransition::Recovered) {
                equal(effective, baseline);
                restored = true;
            }
        }
        if (const auto* route = std::get_if<RoutingDecisionObservation>(&record.observation)) {
            truth(route->capacity_revision.has_value());
        }
    }
    truth(degraded);
    truth(restored);
}
topology::LinkId source_link(const topology::TopologyGraph& graph) {
    for (const auto& arc : graph.outgoing(topology::NodeId{topology::NicId{0}})) {
        if (graph.find(arc.id.link)->kind == topology::LinkKind::Fabric) {
            return arc.id.link;
        }
    }
    throw std::logic_error{"missing source fabric link"};
}
TEST(FailureTelemetryTest, EffectiveHealthRefreshesAfterOtherCauseRestores) {
    auto scenario = workload::parse_scenario(read(example("no-route-recovery.yaml")));
    scenario.telemetry.mode = TelemetryMode::Full;
    auto script = required(scenario.failure_script);
    script.loads.clear();
    const auto graph = topology::generate_clos({64, 8, 8, 8});
    script.incidents = {{topology::SwitchId{0}, sim::SimTimeNs{0}, sim::SimTimeNs{9000},
                         failures::Kind::SwitchDown},
                        {source_link(*graph), sim::SimTimeNs{0}, sim::SimTimeNs{5000}}};
    scenario.failure_script = std::move(script);
    const auto report = workload::run_training(scenario);
    check_failure_state(report);
    bool refreshed = false;
    for (const auto& record : report.telemetry.records) {
        const auto* failure = std::get_if<FailureObservation>(&record.observation);
        if (failure != nullptr && failure->failure.value() == 2 &&
            failure->transition == FailureTransition::StateObserved &&
            record.timestamp.count() == 9000) {
            truth(required(failure->details).effective_operational);
            refreshed = true;
        }
    }
    truth(refreshed);
    equal(metric(report.telemetry, MetricId::FailureStateTransitionTotal,
                 {{MetricLabel::Failure, 2}, {MetricLabel::Resource, 1}})
              .scalar,
          5U);
}
LoadObservation load_state(std::uint64_t ordinal = 0) {
    return {1,
            ordinal,
            1,
            topology::GpuId{0},
            topology::GpuId{1},
            sim::SimTimeNs{1000},
            2,
            1000,
            1000,
            1000,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            1,
            transport::ByteCount{0},
            transport::ByteCount{0},
            transport::ByteCount{0},
            0,
            0,
            0};
}
TEST(FailureTelemetryTest, StateAndSerializedBoundsFailExplicitly) {
    TelemetryConfiguration configuration;
    configuration.limits.metric_series = 1;
    TelemetrySession session{configuration};
    Correlation correlation;
    correlation.traffic_source = 1;
    session.record(sim::SimTimeNs{0}, correlation, load_state());
    rejects<std::length_error>(
        [&] { session.record(sim::SimTimeNs{0}, correlation, load_state(1)); });
    equal(session.snapshot().perturbations.size(), 1U);
    session.finalize(sim::SimTimeNs{0});
    auto snapshot = session.snapshot();
    snapshot.configuration.limits.serialized_bytes = 1;
    TelemetryRunMetadata provenance;
    provenance.routing_policy = "ecmp";
    provenance.script = ScriptMetadata{1, 2, 4, 0, false};
    rejects<std::length_error>(
        [&] { static_cast<void>(serialize_summary_json(snapshot, provenance)); });
    provenance.script.reset();
    rejects<std::invalid_argument>(
        [&] { static_cast<void>(serialize_summary_json(snapshot, provenance)); });
}
TEST(FailureTelemetryTest, CatalogAndReplayRejectUnsupportedOrPartialInputs) {
    require_supported_catalog_version(1);
    require_supported_catalog_version(2);
    rejects<std::invalid_argument>([] { require_supported_catalog_version(0); });
    rejects<std::invalid_argument>([] { require_supported_catalog_version(3); });
    auto scenario = workload::parse_scenario(read(example("no-route-recovery.yaml")));
    scenario.telemetry.mode = TelemetryMode::Full;
    const auto report = workload::run_training(scenario);
    const auto provenance = metadata(scenario, report);
    rejects<std::invalid_argument>(
        [&] { static_cast<void>(reconstruct_full_trace(report.telemetry, provenance, 1)); });
    auto trace = report.telemetry;
    trace.records.erase(trace.records.begin());
    rejects<std::invalid_argument>(
        [&] { static_cast<void>(reconstruct_full_trace(trace, provenance, 2)); });
    trace = report.telemetry;
    trace.configuration.mode = TelemetryMode::Sampled;
    rejects<std::invalid_argument>(
        [&] { static_cast<void>(reconstruct_full_trace(trace, provenance, 2)); });
}
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream input{text};
    std::string line;
    while (std::getline(input, line)) {
        result.push_back(line);
    }
    return result;
}
TEST(FailureTelemetryTest, CliCatalogV2HasTruthfulOutcomesAndTraceFooter) {
    auto yaml = read(example("no-route-recovery.yaml"));
    yaml += "telemetry: {mode: full}\n";
    const auto path = std::filesystem::temp_directory_path() / "nexuslab-catalog-v2.yaml";
    {
        std::ofstream output{path};
        output << yaml;
    }
    const auto filename = path.string();
    const std::array<std::string_view, 4> args{"train", "--file", filename,
                                               "--telemetry-records-jsonl"};
    std::ostringstream output;
    std::ostringstream error;
    equal(cli::run(args, output, error), 1);
    equal(error.str(), std::string{});
    const auto records = lines(output.str());
    const auto header = YAML::Load(records.front());
    const auto footer = YAML::Load(records.back());
    equal(header["telemetry"]["catalog_version"].as<unsigned>(), 2U);
    equal(header["schema_version"].as<unsigned>(), 1U);
    equal(header["domain_success"].as<bool>(), false);
    equal(header["records_complete"].as<bool>(), true);
    equal(header["terminal"]["status"].as<std::string>(), std::string{"completed"});
    std::string prefix;
    for (std::size_t i = 0; i + 1 < records.size(); ++i) {
        prefix += records[i] + "\n";
    }
    std::ostringstream digest;
    digest << std::hex << std::setfill('0') << std::setw(16) << fnv1a64(prefix);
    equal(footer["content_digest_fnv1a64"].as<std::string>(), digest.str());
    truth(footer["record_count"].as<std::size_t>() > 0);
    std::filesystem::remove(path);
}
TEST(FailureTelemetryTest, AdditiveRecordGoldenKeepsSeparateIncidentAndSourceNamespaces) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession session{configuration};
    Correlation failure_correlation;
    failure_correlation.failure = FailureId{1};
    session.record(sim::SimTimeNs{0}, failure_correlation,
                   FailureObservation{
                       FailureId{1}, FailureTransition::Scheduled, FailureResourceKind::Link, 2, 0,
                       FailureDetails{1, sim::SimTimeNs{0}, sim::SimTimeNs{10}, std::nullopt, false,
                                      false, true, 0, 0, 0, 0}});
    Correlation load_correlation;
    load_correlation.traffic_source = 1;
    session.record(sim::SimTimeNs{0}, load_correlation, load_state());
    session.finalize(sim::SimTimeNs{0});
    TelemetryRunMetadata provenance;
    provenance.routing_policy = "ecmp";
    provenance.script = ScriptMetadata{0x1234, 0x5678, 4, 0, false};
    const auto records = lines(serialize_records_jsonl(session.snapshot(), provenance));
    const std::string expected =
        R"({"type":"record","record_id":0,"timestamp_ns":0,"kind":"failure","correlation":{"failure":1},"observation":{"failure":1,"transition":"scheduled","resource_kind":"link","resource_id":2,"affected_bytes":0,"incident_kind":1,"onset_ns":0,"recover_at_ns":10,"bandwidth_bps":null,"applied":false,"recovered":false,"effective_operational":true,"capacity_revision":0,"affected_jobs":0,"dropped_bytes":0,"dropped_chunks":0,"baseline_capacity_bps":null,"effective_capacity_bps":null}})";
    equal(records.at(1), expected);
    const auto load = YAML::Load(records.at(2));
    equal(load["kind"].as<std::string>(), std::string{"load"});
    equal(load["correlation"]["traffic_source"].as<unsigned>(), 1U);
    truth(!load["correlation"]["failure"]);
    equal(load["observation"]["outcome"].as<unsigned>(), 1U);
    truth(load["observation"]["finished_ns"].IsNull());
}

TEST(FailureTelemetryTest, PendingStoppedFactsHaveNoInventedDetectionOrCompletion) {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    TelemetrySession session{configuration};
    auto graph = topology::generate_two_gpu_direct();
    std::vector<transport::DirectedLinkConfiguration> links;
    for (const auto& link : graph->links()) {
        if (link.kind != topology::LinkKind::Fabric) {
            continue;
        }
        for (const auto& arc : topology::directed_links(link)) {
            links.push_back({arc.id, transport::BitsPerSecond{8'000'000'000ULL},
                             sim::SimDurationNs{25}, transport::ByteCount{10000}, std::nullopt});
        }
    }
    transport::TransportRuntime transport{*graph, links};
    routing::Router router{*graph, transport, routing::PolicyRegistry{}};
    failures::Runtime failures{
        failures::Script{{{source_link(*graph), sim::SimTimeNs{10000}, sim::SimTimeNs{20000}}}},
        *graph,
        transport,
        nullptr,
        nullptr,
        {},
        session.sink()};
    traffic::Runtime loads{
        {{traffic::Kind::Burst, topology::GpuId{0}, topology::GpuId{1}, sim::SimTimeNs{1000}, 2,
          sim::SimDurationNs{1000}, transport::ByteCount{1000}, transport::ByteCount{1000}}},
        *graph,
        router,
        {},
        session.sink()};
    sim::Simulation simulation{42, sim::TraceMode::Enabled, session.sink()};
    failures.schedule(simulation);
    loads.schedule(simulation);
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{500}, sim::EventPriority::Normal, sim::NoOpEvent{}}));
    test::NoOpDispatcher dispatcher{
        [](const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
            context.stop(sim::StopReason::Requested);
        }};
    const auto result = simulation.run(dispatcher);
    equal(result.status, sim::SimulationStatus::Stopped);
    session.finalize(result.final_time);
    const auto snapshot = session.snapshot();
    equal(snapshot.perturbations.size(), 3U);
    truth(!has_metric(snapshot, MetricId::FailureDetectionTimeNs));
    truth(!has_metric(snapshot, MetricId::FailureRecoveryTimeNs));
    const auto& pending = std::get<LoadObservation>(snapshot.perturbations.at(1).observation);
    equal(pending.outcome, 1U);
    truth(!pending.injected.has_value());
    truth(!pending.finished.has_value());
    TelemetryRunMetadata provenance;
    provenance.routing_policy = "ecmp";
    provenance.final_time = result.final_time;
    provenance.outcome = TelemetryRunOutcome::Stopped;
    provenance.complete = false;
    provenance.script = ScriptMetadata{1, 2, 4, 0, false};
    const auto rebuilt = reconstruct_full_trace(snapshot, provenance, 2);
    equal(rebuilt.perturbations, snapshot.perturbations);
    equal(serialize_summary_json(rebuilt, provenance),
          serialize_summary_json(snapshot, provenance));
}
TEST(FailureTelemetryTest, LegacyCatalogOneReconstructsWithoutAdditiveFields) {
    auto scenario = workload::parse_scenario(read(example("two-worker.yaml")));
    scenario.telemetry.mode = TelemetryMode::Full;
    const auto report = workload::run_training(scenario);
    TelemetryRunMetadata provenance;
    provenance.routing_policy = scenario.routing_policy;
    provenance.final_time = report.simulation.final_time;
    const auto rebuilt = reconstruct_full_trace(report.telemetry, provenance, 1);
    equal(serialize_summary_json(rebuilt, provenance),
          serialize_summary_json(report.telemetry, provenance));
    equal(rebuilt.perturbations.size(), 0U);
    const auto header = YAML::Load(lines(serialize_records_jsonl(rebuilt, provenance)).front());
    equal(header["telemetry"]["catalog_version"].as<unsigned>(), 1U);
    truth(!header["domain_success"]);
}
TEST(FailureTelemetryTest, NoRouteCauseJoinsTheActualRoutingDecisionAndCollective) {
    auto scenario = workload::parse_scenario(read(example("no-route-recovery.yaml")));
    scenario.telemetry.mode = TelemetryMode::Full;
    const auto report = workload::run_training(scenario);
    std::map<std::uint64_t, sim::EventId> rejected;
    for (const auto& record : report.telemetry.records) {
        if (const auto* route = std::get_if<RoutingDecisionObservation>(&record.observation);
            route != nullptr && route->outcome == RoutingDecisionOutcome::NoRoute) {
            const auto request = required(route->request);
            equal(request.flow, required(record.correlation.flow));
            rejected.emplace(request.flow, required(record.correlation.event));
        }
    }
    std::size_t attributed{0};
    for (const auto& record : report.telemetry.records) {
        if (const auto* failure = std::get_if<FailureObservation>(&record.observation);
            failure != nullptr && failure->transition == FailureTransition::AdmissionRejected) {
            const auto flow = required(record.correlation.flow);
            equal(rejected.at(flow), required(record.correlation.event));
            equal(required(record.correlation.job).value(), 0U);
            equal(required(record.correlation.collective).value(), 0U);
            ++attributed;
        }
    }
    equal(rejected.size(), 2U);
    equal(attributed, 1U);
}
} // namespace
} // namespace nexuslab::telemetry
