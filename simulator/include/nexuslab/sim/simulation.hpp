// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/sim/deterministic_rng.hpp"
#include "nexuslab/sim/event.hpp"
#include "nexuslab/sim/event_queue.hpp"
#include "nexuslab/sim/trace.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace nexuslab::sim {

enum class SimulationStatus : std::uint8_t {
    Completed,
    Stopped,
    Failed,
};

struct SimulationResult final {
    SimulationStatus status;
    std::optional<StopReason> stop_reason;
    SimTimeNs final_time;
    std::uint64_t dispatched_events;
    std::uint64_t cancelled_events;
    std::size_t pending_events;
    std::uint64_t rng_draw_count;
    std::optional<std::uint64_t> trace_hash;
    std::optional<std::string> error;

    bool operator==(const SimulationResult&) const = default;
};

class Simulation;

class SimulationContext final {
  public:
    [[nodiscard]] EventId schedule(EventSpec specification);
    [[nodiscard]] bool cancel(EventId id);
    void stop(StopReason reason);

    [[nodiscard]] SimTimeNs now() const noexcept;
    [[nodiscard]] EventId current_event_id() const;
    [[nodiscard]] std::optional<EventId> cause() const;
    [[nodiscard]] std::uint64_t random_u64() noexcept;
    [[nodiscard]] std::uint64_t random_below(std::uint64_t upper_exclusive);

  private:
    friend class Simulation;

    explicit SimulationContext(Simulation& simulation) noexcept;

    Simulation* simulation_;
};

class Simulation final {
  public:
    explicit Simulation(std::uint64_t seed, TraceMode trace_mode = TraceMode::Enabled);
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Simulation(Simulation&&) = delete;
    Simulation& operator=(Simulation&&) = delete;

    [[nodiscard]] EventId schedule(EventSpec specification);
    [[nodiscard]] bool cancel(EventId id);
    void stop(StopReason reason);

    [[nodiscard]] SimTimeNs now() const noexcept;
    [[nodiscard]] std::span<const TraceRecord> trace_records() const noexcept;

    template <typename Dispatcher> [[nodiscard]] SimulationResult run(Dispatcher& dispatcher) {
        static_assert(
            std::is_invocable_v<Dispatcher&, const NoOpEvent&, SimulationContext&> &&
                std::is_invocable_v<Dispatcher&, const transport::ChunkArrivalEvent&,
                                    SimulationContext&> &&
                std::is_invocable_v<Dispatcher&, const transport::TransmissionCompleteEvent&,
                                    SimulationContext&> &&
                std::is_invocable_v<Dispatcher&, const transport::LinkStateChangeEvent&,
                                    SimulationContext&>,
            "dispatcher must handle every EventPayload alternative with SimulationContext");

        begin_run();
        try {
            while (!stop_reason_.has_value()) {
                auto next = next_dispatchable_event();
                if (!next.has_value()) {
                    return finish(SimulationStatus::Completed, std::nullopt);
                }

                current_event_ = next;
                now_ = next->timestamp;
                ++dispatched_events_;
                if (trace_.enabled()) {
                    record_event(TraceAction::Dispatched, now_, event_trace_metadata(*next));
                }

                SimulationContext context{*this};
                std::visit([&dispatcher, &context](
                               const auto& payload) { std::invoke(dispatcher, payload, context); },
                           next->payload);
                current_event_.reset();
            }
            return finish(SimulationStatus::Stopped, std::nullopt);
        } catch (const std::exception& error) {
            return fail(error.what());
        } catch (...) {
            return fail("unknown simulation handler failure");
        }
    }

  private:
    friend class SimulationContext;

    enum class Lifecycle : std::uint8_t {
        Created,
        Running,
        Finished,
    };

    struct EventTraceMetadata final {
        SimTimeNs timestamp;
        EventPriority priority;
        EventId id;
        std::optional<EventId> cause;
        EventPayloadKind payload_kind;
    };

    [[nodiscard]] static EventTraceMetadata event_trace_metadata(const Event& event);
    void record_event(TraceAction action, SimTimeNs recorded_at, const EventTraceMetadata& metadata,
                      std::optional<StopReason> stop_reason = std::nullopt);
    void record_terminal(TraceAction action, const std::optional<std::string>& error);
    void begin_run();
    [[nodiscard]] std::optional<Event> next_dispatchable_event();
    [[nodiscard]] SimulationResult finish(SimulationStatus status,
                                          std::optional<std::string> error);
    [[nodiscard]] SimulationResult fail(std::string error);
    [[nodiscard]] EventId current_event_id() const;
    [[nodiscard]] std::optional<EventId> current_cause() const;
    [[nodiscard]] std::uint64_t random_u64() noexcept;
    [[nodiscard]] std::uint64_t random_below(std::uint64_t upper_exclusive);

    SimTimeNs now_;
    EventIdGenerator event_ids_;
    EventQueue event_queue_;
    DeterministicRng rng_;
    TraceLog trace_;
    std::unordered_set<std::uint64_t> pending_ids_;
    std::unordered_set<std::uint64_t> cancelled_ids_;
    std::unordered_map<std::uint64_t, EventTraceMetadata> trace_metadata_;
    std::optional<Event> current_event_;
    std::optional<StopReason> stop_reason_;
    Lifecycle lifecycle_{Lifecycle::Created};
    std::uint64_t dispatched_events_{0};
    std::uint64_t cancelled_events_{0};
};

} // namespace nexuslab::sim
