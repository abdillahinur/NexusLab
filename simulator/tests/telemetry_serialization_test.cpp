// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#include "nexuslab/telemetry/serialization.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace nexuslab::telemetry {
namespace {

using ::testing::HasSubstr;

[[nodiscard]] TelemetryRunMetadata metadata() {
    TelemetryRunMetadata result;
    result.seed = 42;
    result.scenario_digest = 0x1234U;
    result.routing_policy = "ecmp";
    result.scheduling_policy = "first-fit";
    result.final_time = sim::SimTimeNs{20};
    return result;
}

[[nodiscard]] TelemetrySnapshot populated_snapshot() {
    TelemetryConfiguration configuration;
    configuration.mode = TelemetryMode::Full;
    configuration.sample_interval_ns = 10;
    TelemetrySession session{configuration};
    Correlation correlation;
    correlation.event = sim::EventId{3};
    session.record(sim::SimTimeNs{0}, correlation,
                   CounterObservation{MetricId::SimulationDispatchedEvents, {}, 2});
    session.record(sim::SimTimeNs{20}, {},
                   GaugeObservation{MetricId::SimulationFinalTimeNs, {}, 20});
    session.finalize(sim::SimTimeNs{20});
    return session.snapshot();
}

[[nodiscard]] std::vector<YAML::Node> json_lines(std::string_view jsonl) {
    std::vector<YAML::Node> result;
    std::size_t begin = 0;
    while (begin < jsonl.size()) {
        const std::size_t end = jsonl.find('\n', begin);
        if (end == std::string_view::npos) {
            throw std::logic_error{"JSON Lines output lacks a final newline"};
        }
        result.push_back(YAML::Load(std::string{jsonl.substr(begin, end - begin)}));
        begin = end + 1;
    }
    return result;
}

TEST(TelemetrySerializationTest, EmitsCanonicalVersionedSummaryAndRecordsDocuments) {
    const TelemetrySnapshot snapshot = populated_snapshot();
    const std::string summary = serialize_summary_json(snapshot, metadata());
    const std::string records = serialize_records_jsonl(snapshot, metadata());

    const YAML::Node document = YAML::Load(summary);
    EXPECT_EQ(document["schema"].as<std::string>(), "nexuslab.telemetry.summary");
    EXPECT_EQ(document["schema_version"].as<std::uint32_t>(), telemetry_schema_version);
    EXPECT_EQ(document["telemetry"]["catalog_version"].as<std::uint32_t>(),
              telemetry_catalog_version);
    EXPECT_EQ(document["metric_definitions"].size(), metric_catalog().size());
    EXPECT_EQ(document["metrics"].size(), snapshot.metrics.size());
    EXPECT_EQ(summary.back(), '\n');

    const std::vector<YAML::Node> lines = json_lines(records);
    ASSERT_EQ(lines.size(), snapshot.records.size() + snapshot.samples.size() + 2U);
    EXPECT_EQ(lines.front()["type"].as<std::string>(), "header");
    EXPECT_EQ(lines[1]["type"].as<std::string>(), "record");
    EXPECT_EQ(lines.back()["type"].as<std::string>(), "footer");
    EXPECT_EQ(lines.back()["record_count"].as<std::size_t>(), snapshot.records.size());
    EXPECT_EQ(lines.back()["sample_count"].as<std::size_t>(), snapshot.samples.size());
    EXPECT_EQ(serialize_summary_json(snapshot, metadata()), summary);
    EXPECT_EQ(serialize_records_jsonl(snapshot, metadata()), records);

    // Version-1 compatibility sentinels. Change only with an intentional schema-version decision.
    EXPECT_EQ(fnv1a64(summary), 0x1964DC82CC5227C8ULL);
    EXPECT_EQ(fnv1a64(records), 0xA0CADFEA88F5BA90ULL);
}

TEST(TelemetrySerializationTest, EscapesTextAndSerializesEveryObservationKind) {
    TelemetrySnapshot snapshot;
    snapshot.configuration.mode = TelemetryMode::Full;
    snapshot.finalized = true;
    Correlation correlation;
    correlation.link = topology::DirectedLinkId{topology::LinkId{7}, topology::LinkDirection::BToA};
    correlation.job = workload::JobId{1};
    const std::array path{
        topology::DirectedLinkId{topology::LinkId{7}, topology::LinkDirection::BToA}};
    const std::array workers{topology::GpuId{2}};
    const auto add = [&](TelemetryObservation observation) {
        const auto id = TelemetryRecordId{snapshot.records.size()};
        snapshot.records.push_back({correlation, std::move(observation), id, sim::SimTimeNs{0}});
    };

    add(CounterObservation{MetricId::SimulationDispatchedEvents, {}, 1});
    add(GaugeObservation{MetricId::SimulationFinalTimeNs, {}, 0});
    add(HistogramObservation{MetricId::JobCompletionTimeNs, {}, 0});
    add(SimulationObservation{SimulationTransition::RunStarted, 1});
    add(JobObservation{JobTransition::Arrived, 0, 0, 0, 1});
    add(CollectiveObservation{CollectiveTransition::Submitted, collective::Phase::ReduceScatter, 0,
                              10});
    add(TransferObservation{TransferTransition::ChunkDropped, 10, 1, TransferReason::BufferFull});
    add(QueueObservation{QueueTransition::Dropped, 10, 20, 2});
    add(RoutingDecisionObservation{RoutingDecisionId{4},
                                   "custom\"policy",
                                   1,
                                   2,
                                   1,
                                   {path.begin(), path.end()},
                                   3,
                                   "line\nreason",
                                   transport::TransferId{5},
                                   RoutingDecisionOutcome::Selected});
    add(PlacementDecisionObservation{PlacementDecisionId{6},
                                     "first-fit",
                                     1,
                                     0,
                                     1,
                                     {workers.begin(), workers.end()},
                                     1,
                                     1,
                                     0,
                                     "placed",
                                     PlacementDecisionOutcome::Placed});
    add(FailureObservation{FailureId{8}, FailureTransition::Applied, FailureResourceKind::Link, 7,
                           10});
    snapshot.samples.push_back(
        {MetricId::SimulationFinalTimeNs, {}, MetricKind::Gauge, 0, sim::SimTimeNs{0}});

    const std::string output = serialize_records_jsonl(snapshot, metadata());

    for (const std::string_view kind :
         {"counter", "gauge", "histogram", "simulation", "job", "collective", "transfer", "queue",
          "routing_decision", "placement_decision", "failure"}) {
        EXPECT_THAT(output, HasSubstr("\"kind\":\"" + std::string{kind} + "\""));
    }
    EXPECT_THAT(output, HasSubstr("\"link\":{\"link\":7,\"direction\":2}"));
    EXPECT_THAT(output, HasSubstr("custom\\\"policy"));
    EXPECT_THAT(output, HasSubstr("line\\nreason"));
    EXPECT_THAT(output, HasSubstr("\"type\":\"sample\""));
}

TEST(TelemetrySerializationTest, RequiresFinalizationAndEnforcesSerializedByteLimit) {
    TelemetrySnapshot snapshot = populated_snapshot();
    snapshot.finalized = false;
    EXPECT_THROW(static_cast<void>(serialize_summary_json(snapshot, metadata())), std::logic_error);

    snapshot.finalized = true;
    snapshot.configuration.limits.serialized_bytes = 10;
    EXPECT_THROW(static_cast<void>(serialize_summary_json(snapshot, metadata())),
                 std::length_error);
    EXPECT_THROW(static_cast<void>(serialize_records_jsonl(snapshot, metadata())),
                 std::length_error);
}

TEST(TelemetrySerializationTest, AcceptsOnlyTheSupportedMajorSchemaVersion) {
    EXPECT_NO_THROW(require_supported_schema_version(telemetry_schema_version));
    EXPECT_THROW(require_supported_schema_version(0), std::invalid_argument);
    EXPECT_THROW(require_supported_schema_version(telemetry_schema_version + 1),
                 std::invalid_argument);
}

} // namespace
} // namespace nexuslab::telemetry
