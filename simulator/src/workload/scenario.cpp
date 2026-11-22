// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/workload/scenario.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
namespace nexuslab::workload {
std::vector<Profile> profiles() {
    return {
        {"small-data-parallel", "synthetic short compute and small gradient", 3,
         sim::SimDurationNs{10'000}, transport::ByteCount{65'536}, transport::ByteCount{65'536}},
        {"large-llm", "synthetic large gradient; no parameter or optimizer memory model", 10,
         sim::SimDurationNs{100'000'000}, transport::ByteCount{67'108'864},
         transport::ByteCount{8'388'608}},
        {"communication-heavy", "synthetic short compute with large collective", 3,
         sim::SimDurationNs{1'000}, transport::ByteCount{1'048'576}, transport::ByteCount{262'144}},
        {"compute-heavy", "synthetic long compute with small collective", 3,
         sim::SimDurationNs{1'000'000}, transport::ByteCount{65'536}, transport::ByteCount{65'536}},
        {"bursty-moe", "arrival/compute bursts only; no expert dispatch or AllToAll model", 3,
         sim::SimDurationNs{5'000}, transport::ByteCount{262'144}, transport::ByteCount{65'536}},
        {"checkpoint-heavy", "large synchronization volume proxy; no storage/checkpoint engine", 3,
         sim::SimDurationNs{100'000}, transport::ByteCount{4'194'304},
         transport::ByteCount{1'048'576}},
        {"inference-burst", "small compute/synchronization proxy; no autoregressive token model", 3,
         sim::SimDurationNs{1'000}, transport::ByteCount{4'096}, transport::ByteCount{4'096}}};
}
namespace {
void keys(const YAML::Node& node, std::initializer_list<std::string_view> allowed) {
    if (!node.IsMap()) {
        throw std::invalid_argument{"scenario requires mapping"};
    }
    std::set<std::string> seen;
    for (const auto& entry : node) {
        const auto key = entry.first.as<std::string>();
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end() ||
            !seen.insert(key).second) {
            throw std::invalid_argument{"unknown or duplicate scenario key: " + key};
        }
    }
}
[[nodiscard]] std::uint64_t number(const YAML::Node& node) {
    if (!node.IsScalar()) {
        throw std::invalid_argument{"scenario quantity must be scalar integer"};
    }
    const auto& text = node.Scalar();
    if (text.size() > 20) {
        throw std::invalid_argument{"scenario integer exceeds 20 digits"};
    }
    std::uint64_t value{0};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument{"invalid scenario integer"};
    }
    return value;
}
[[nodiscard]] std::uint64_t value(const YAML::Node& node, const char* key, std::uint64_t fallback) {
    return node[key] ? number(node[key]) : fallback;
}
[[nodiscard]] std::uint32_t narrow(std::uint64_t number_value) {
    if (number_value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument{"scenario integer exceeds uint32 range"};
    }
    return static_cast<std::uint32_t>(number_value);
}
[[nodiscard]] std::size_t narrow_size(std::uint64_t number_value) {
    if (number_value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument{"scenario integer exceeds size range"};
    }
    return static_cast<std::size_t>(number_value);
}

[[nodiscard]] telemetry::TelemetryConfiguration read_telemetry(const YAML::Node& node) {
    telemetry::TelemetryConfiguration result;
    if (!node) {
        return result;
    }
    keys(node, {"mode", "sample_interval_ns", "limits"});
    if (!node["mode"] || !node["mode"].IsScalar()) {
        throw std::invalid_argument{"telemetry mode must be a scalar"};
    }
    result.mode = telemetry::parse_mode(node["mode"].Scalar());
    const YAML::Node limits = node["limits"];
    if (result.mode == telemetry::TelemetryMode::Off && (node["sample_interval_ns"] || limits)) {
        throw std::invalid_argument{"off telemetry accepts only its mode"};
    }
    if ((result.mode == telemetry::TelemetryMode::Summary) && node["sample_interval_ns"]) {
        throw std::invalid_argument{"summary telemetry does not sample"};
    }
    if (node["sample_interval_ns"]) {
        result.sample_interval_ns = number(node["sample_interval_ns"]);
    }
    if (limits) {
        keys(limits, {"metric_series", "decision_records", "domain_records", "samples",
                      "histogram_boundaries", "correlation_edges", "serialized_bytes"});
        if (result.mode == telemetry::TelemetryMode::Summary &&
            (limits["decision_records"] || limits["domain_records"] || limits["samples"] ||
             limits["correlation_edges"])) {
            throw std::invalid_argument{"summary telemetry accepts only summary limits"};
        }
        if (result.mode == telemetry::TelemetryMode::Sampled && limits["domain_records"]) {
            throw std::invalid_argument{"sampled telemetry does not retain domain records"};
        }
        result.limits.metric_series =
            narrow_size(value(limits, "metric_series", result.limits.metric_series));
        result.limits.decision_records =
            narrow_size(value(limits, "decision_records", result.limits.decision_records));
        result.limits.domain_records =
            narrow_size(value(limits, "domain_records", result.limits.domain_records));
        result.limits.samples = narrow_size(value(limits, "samples", result.limits.samples));
        result.limits.histogram_boundaries =
            narrow_size(value(limits, "histogram_boundaries", result.limits.histogram_boundaries));
        result.limits.correlation_edges =
            narrow_size(value(limits, "correlation_edges", result.limits.correlation_edges));
        result.limits.serialized_bytes =
            value(limits, "serialized_bytes", result.limits.serialized_bytes);
    }
    telemetry::validate_configuration(result);
    return result;
}
[[nodiscard]] failures::Kind read_failure_kind(std::string_view text) {
    for (const auto kind :
         {failures::Kind::LinkDown, failures::Kind::LinkDegraded, failures::Kind::PortDown,
          failures::Kind::SwitchDown, failures::Kind::GpuDown}) {
        if (failures::kind_name(kind) == text) {
            return kind;
        }
    }
    throw std::invalid_argument{"unknown failure kind"};
}
[[nodiscard]] failures::Target read_failure_target(failures::Kind kind, std::uint64_t id) {
    switch (kind) {
    case failures::Kind::PortDown:
        return topology::PortId{id};
    case failures::Kind::SwitchDown:
        return topology::SwitchId{id};
    case failures::Kind::GpuDown:
        return topology::GpuId{id};
    default:
        return topology::LinkId{id};
    }
}
[[nodiscard]] failures::Incident read_incident(const YAML::Node& incident) {
    keys(incident, {"kind", "target", "at_ns", "recover_at_ns", "bandwidth_bps"});
    const auto kind = read_failure_kind(incident["kind"].as<std::string>());
    const auto target = read_failure_target(kind, number(incident["target"]));
    const auto bandwidth =
        incident["bandwidth_bps"]
            ? std::optional{transport::BitsPerSecond{number(incident["bandwidth_bps"])}}
            : std::nullopt;
    if ((kind == failures::Kind::LinkDegraded) != bandwidth.has_value() ||
        (bandwidth.has_value() && bandwidth->value() == 0)) {
        throw std::invalid_argument{"only degraded links require positive bandwidth_bps"};
    }
    return {target, sim::SimTimeNs{number(incident["at_ns"])},
            incident["recover_at_ns"]
                ? std::optional{sim::SimTimeNs{number(incident["recover_at_ns"])}}
                : std::nullopt,
            kind, bandwidth};
}
[[nodiscard]] traffic::Source read_load(const YAML::Node& node) {
    keys(node, {"kind", "source_gpu", "destination_gpu", "at_ns", "count", "interval_ns",
                "bytes_per_transfer", "chunk_bytes"});
    for (const auto* key : {"kind", "source_gpu", "destination_gpu", "at_ns", "count",
                            "interval_ns", "bytes_per_transfer", "chunk_bytes"}) {
        if (!node[key]) {
            throw std::invalid_argument{"missing background load field"};
        }
    }
    if (!node["kind"].IsScalar()) {
        throw std::invalid_argument{"load kind required"};
    }
    const auto kind = node["kind"].Scalar();
    if (kind != "burst" && kind != "checkpoint") {
        throw std::invalid_argument{"unknown background load kind"};
    }
    return {kind == "burst" ? traffic::Kind::Burst : traffic::Kind::Checkpoint,
            topology::GpuId{number(node["source_gpu"])},
            topology::GpuId{number(node["destination_gpu"])},
            sim::SimTimeNs{number(node["at_ns"])},
            number(node["count"]),
            sim::SimDurationNs{number(node["interval_ns"])},
            transport::ByteCount{number(node["bytes_per_transfer"])},
            transport::ByteCount{number(node["chunk_bytes"])}};
}
[[nodiscard]] failures::Script read_failures(const YAML::Node& node) {
    keys(node, {"version", "detection", "incidents", "loads"});
    if (number(node["version"]) != 1 || !node["detection"].IsScalar() ||
        node["detection"].Scalar() != "immediate") {
        throw std::invalid_argument{"failure script requires version 1 and immediate detection"};
    }
    const auto loads = node["loads"];
    if (loads && (!loads.IsSequence() || loads.size() > 10'000)) {
        throw std::invalid_argument{"background loads require a bounded sequence"};
    }
    const auto incidents = node["incidents"];
    if (!incidents.IsSequence() || incidents.size() > 10'000) {
        throw std::invalid_argument{"failure incidents require a bounded sequence"};
    }
    failures::Script result;
    if (loads) {
        for (const auto& load : loads) {
            result.loads.push_back(read_load(load));
        }
    }
    for (const auto& incident : incidents) {
        result.incidents.push_back(read_incident(incident));
    }
    return result;
}
void read_compute(const YAML::Node& node, sim::SimDurationNs default_compute, JobSpec& result) {
    const auto workers = node["workers"];
    const auto requested = result.requested_workers;
    const auto count = requested != 0 ? static_cast<std::size_t>(requested) : workers.size();
    const auto compute = node["compute_ns"];
    if (compute && compute.IsSequence() && compute.size() != count) {
        throw std::invalid_argument{"compute durations must match workers"};
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (requested == 0) {
            result.workers.emplace_back(number(workers[index]));
        }
        const auto duration = !compute ? default_compute.count()
                                       : number(compute.IsSequence() ? compute[index] : compute);
        result.compute.emplace_back(duration);
    }
}
[[nodiscard]] JobSpec job(const YAML::Node& node) {
    keys(node, {"name", "profile", "workers", "requested_workers", "arrival_ns", "steps",
                "compute_ns", "gradient_bytes", "bucket_bytes", "chunk_bytes", "priority",
                "overlap", "collective", "algorithm"});
    const auto name = node["profile"] ? node["profile"].as<std::string>() : "small-data-parallel";
    const auto available = profiles();
    const auto found = std::find_if(available.begin(), available.end(),
                                    [&](const auto& candidate) { return candidate.name == name; });
    if (found == available.end()) {
        throw std::invalid_argument{"unknown synthetic workload profile"};
    }
    if ((node["collective"] && node["collective"].as<std::string>() != "allreduce") ||
        (node["algorithm"] && node["algorithm"].as<std::string>() != "ring")) {
        throw std::invalid_argument{"unsupported collective or algorithm"};
    }
    const auto workers = node["workers"];
    const auto requested = value(node, "requested_workers", 0);
    if ((node["requested_workers"] && (workers || requested == 0)) || requested > 8192 ||
        (requested == 0 &&
         (!workers.IsSequence() || workers.size() == 0 || workers.size() > 8192))) {
        throw std::invalid_argument{"invalid worker list"};
    }
    JobSpec result;
    result.requested_workers = narrow(requested);
    result.name = node["name"] ? node["name"].as<std::string>() : name;
    if (result.name.size() > 256) {
        throw std::length_error{"job name exceeds 256 bytes"};
    }
    result.arrival = sim::SimTimeNs{value(node, "arrival_ns", 0)};
    result.steps = narrow(value(node, "steps", found->steps));
    result.gradient_bytes =
        transport::ByteCount{value(node, "gradient_bytes", found->gradient.value())};
    result.bucket_bytes = transport::ByteCount{value(node, "bucket_bytes", found->bucket.value())};
    result.chunk_bytes = transport::ByteCount{value(node, "chunk_bytes", 4096)};
    result.priority = narrow(value(node, "priority", 0));
    if (node["overlap"]) {
        const auto flag = node["overlap"].as<std::string>();
        if (flag != "true" && flag != "false") {
            throw std::invalid_argument{"overlap must be true or false"};
        }
        result.overlap = flag == "true";
    }
    read_compute(node, found->compute, result);
    return result;
}
[[nodiscard]] std::vector<JobControl> read_controls(const YAML::Node& controls,
                                                    std::size_t job_count) {
    std::vector<JobControl> result;
    if (controls) {
        if (!controls.IsSequence() || controls.size() > 10'000) {
            throw std::invalid_argument{"invalid job controls"};
        }
        for (const auto& control : controls) {
            keys(control, {"job", "kind", "at_ns", "worker"});
            const auto kind = control["kind"].as<std::string>();
            if (kind != "cancel" && kind != "worker_failure") {
                throw std::invalid_argument{"unknown job control kind"};
            }
            const auto index = number(control["job"]);
            if (index >= job_count) {
                throw std::invalid_argument{"unknown controlled job"};
            }
            result.push_back(
                {static_cast<std::size_t>(index),
                 kind == "cancel" ? WorkloadEventKind::Cancel : WorkloadEventKind::WorkerFailure,
                 sim::SimTimeNs{number(control["at_ns"])}, narrow(value(control, "worker", 0))});
        }
    }
    return result;
}
std::vector<GpuControl> read_gpu_controls(const YAML::Node& controls, std::size_t gpus,
                                          bool scheduling) {
    std::vector<GpuControl> result;
    if (!controls) {
        return result;
    }
    if (!scheduling || !controls.IsSequence() || controls.size() > 10'000) {
        throw std::invalid_argument{"GPU controls require scheduler and bounded list"};
    }
    for (const auto& control : controls) {
        keys(control, {"gpu", "state", "at_ns"});
        const auto gpu = number(control["gpu"]);
        const auto state = control["state"].as<std::string>();
        if (gpu >= gpus || (state != "up" && state != "down")) {
            throw std::invalid_argument{"invalid GPU control"};
        }
        result.push_back(
            {topology::GpuId{gpu}, state == "up", sim::SimTimeNs{number(control["at_ns"])}});
    }
    return result;
}
} // namespace
void validate_failure_controls(const TrainingScenario& scenario) {
    if (!scenario.failure_script.has_value()) {
        return;
    }
    for (const auto& incident : scenario.failure_script->incidents) {
        if (incident.kind != failures::Kind::GpuDown) {
            continue;
        }
        if (!scenario.scheduling.has_value()) {
            throw std::invalid_argument{"GPU failure requires scheduling_policy"};
        }
        const auto* gpu = std::get_if<topology::GpuId>(&incident.target);
        if (gpu == nullptr) {
            throw std::invalid_argument{"GPU incident requires a GPU target"};
        }
        for (const auto& control : scenario.gpu_controls) {
            if (control.gpu == *gpu) {
                throw std::invalid_argument{"scripted GPU conflicts with gpu_controls"};
            }
        }
    }
}
TrainingScenario parse_scenario(std::string_view yaml) {
    if (yaml.size() > 1'048'576) {
        throw std::length_error{"scenario exceeds one MiB"};
    }
    const auto root = YAML::Load(std::string{yaml});
    keys(root, {"version", "gpus", "seed", "routing_policy", "bandwidth_bps", "propagation_ns",
                "buffer_bytes", "scheduling_policy", "gpu_controls", "local_bandwidth_bps",
                "local_latency_ns", "telemetry", "jobs", "controls", "failure_script"});
    if (number(root["version"]) != 1) {
        throw std::invalid_argument{"unsupported training scenario version"};
    }
    TrainingScenario result;
    result.gpus = static_cast<std::size_t>(value(root, "gpus", 64));
    if (result.gpus < 64 || result.gpus > 8192 || result.gpus % 64 != 0) {
        throw std::invalid_argument{"unsupported Clos GPU count"};
    }
    result.seed = value(root, "seed", 42);
    if (root["scheduling_policy"]) {
        result.scheduling = scheduling::Configuration{};
        result.scheduling->policy = root["scheduling_policy"].as<std::string>();
        result.scheduling->seed = result.seed;
        static_cast<void>(scheduling::make_policy(result.scheduling->policy, result.seed));
    }
    if (root["routing_policy"]) {
        result.routing_policy = root["routing_policy"].as<std::string>();
    }
    result.bandwidth = transport::BitsPerSecond{value(root, "bandwidth_bps", 100'000'000'000ULL)};
    result.propagation = sim::SimDurationNs{value(root, "propagation_ns", 500)};
    result.buffer = transport::ByteCount{value(root, "buffer_bytes", 262'144)};
    result.local.local_bandwidth =
        transport::BitsPerSecond{value(root, "local_bandwidth_bps", 800'000'000'000ULL)};
    result.local.local_latency = sim::SimDurationNs{value(root, "local_latency_ns", 0)};
    const auto jobs = root["jobs"];
    if (!jobs.IsSequence() || jobs.size() == 0 || jobs.size() > 10'000) {
        throw std::invalid_argument{"invalid scenario jobs"};
    }
    std::size_t worker_entries{0};
    for (const auto& entry : jobs) {
        auto specification = job(entry);
        if (specification.compute.size() > 1'000'000 - worker_entries) {
            throw std::length_error{"scenario worker-entry limit exceeded"};
        }
        worker_entries += specification.compute.size();
        if (specification.requested_workers != 0 && !result.scheduling.has_value()) {
            throw std::invalid_argument{"requested_workers requires scheduling_policy"};
        }
        result.jobs.push_back(std::move(specification));
    }
    result.controls = read_controls(root["controls"], result.jobs.size());
    result.gpu_controls =
        read_gpu_controls(root["gpu_controls"], result.gpus, result.scheduling.has_value());
    result.telemetry = read_telemetry(root["telemetry"]);
    if (root["failure_script"]) {
        result.failure_script = read_failures(root["failure_script"]);
    }
    validate_failure_controls(result);
    return result;
}
} // namespace nexuslab::workload
