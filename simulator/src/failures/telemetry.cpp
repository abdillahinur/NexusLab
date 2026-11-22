// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/failures/runtime.hpp"
namespace nexuslab::failures {
void Runtime::emit(std::size_t index, telemetry::FailureTransition kind, sim::SimTimeNs now,
                   telemetry::Correlation correlation, std::uint64_t bytes) {
    if (!telemetry_.enabled()) {
        return;
    }
    const auto& state = report_.incidents.at(index);
    const auto& spec = state.specification;
    const auto resource = static_cast<telemetry::FailureResourceKind>(spec.target.index() + 1);
    correlation.failure = state.id;
    std::optional<std::array<std::uint64_t, 2>> baseline;
    std::optional<std::array<std::uint64_t, 2>> effective;
    if (const auto* link = std::get_if<topology::LinkId>(&spec.target)) {
        const auto& original = baseline_.at(*link);
        baseline = std::array{original[0].value(), original[1].value()};
        effective = std::array{transport_->find_service({*link, topology::LinkDirection::AToB})
                                   ->queue()
                                   .configuration()
                                   .bandwidth.value(),
                               transport_->find_service({*link, topology::LinkDirection::BToA})
                                   ->queue()
                                   .configuration()
                                   .bandwidth.value()};
    }
    telemetry_.record(
        now, correlation,
        telemetry::FailureObservation{
            state.id, kind, resource, target_value(spec.target), bytes,
            telemetry::FailureDetails{
                static_cast<std::uint64_t>(spec.kind), spec.onset, spec.recovery,
                spec.bandwidth.has_value() ? std::optional{spec.bandwidth->value()} : std::nullopt,
                state.applied, state.recovered, operational(spec), transport_->capacity_revision(),
                state.affected_jobs.size(), state.dropped.bytes, state.dropped.chunks, baseline,
                effective}});
}
void Runtime::refresh(sim::SimTimeNs now) {
    if (!telemetry_.enabled()) {
        return;
    }
    for (std::size_t i = 0; i < report_.incidents.size(); ++i) {
        auto& state = report_.incidents[i];
        const auto health = operational(state.specification);
        const auto revision = transport_->capacity_revision();
        if (state.effective_operational != health || state.capacity_revision != revision) {
            state.effective_operational = health;
            state.capacity_revision = revision;
            emit(i, telemetry::FailureTransition::StateObserved, now, action_correlation_);
        }
    }
}
void Runtime::emit_counter(std::size_t index, telemetry::MetricId metric, std::uint64_t amount,
                           sim::SimTimeNs now) {
    if (!telemetry_.enabled()) {
        return;
    }
    const auto& state = report_.incidents.at(index);
    auto correlation = action_correlation_;
    correlation.failure = state.id;
    telemetry_.record(
        now, correlation,
        telemetry::CounterObservation{
            metric,
            {{telemetry::MetricLabel::Failure, state.id.value()},
             {telemetry::MetricLabel::Resource, state.specification.target.index() + 1}},
            amount});
}
} // namespace nexuslab::failures
