// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0
#include "nexuslab/routing/router.hpp"
#include "nexuslab/sim/simulation.hpp"
#include "nexuslab/topology/clos.hpp"
#include "nexuslab/topology/families.hpp"
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <set>
#include <source_location>

namespace nexuslab::routing {
namespace {
template <typename Actual, typename Expected>
void equal(const Actual& actual, const Expected& expected,
           std::source_location location = std::source_location::current()) {
    SCOPED_TRACE(location.line());
    EXPECT_EQ(actual, expected);
}
template <typename Exception, typename Operation> [[nodiscard]] bool throws(Operation operation) {
    try {
        operation();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}
[[nodiscard]] Endpoints endpoints(std::uint64_t destination = 8) {
    return {topology::NodeId{topology::NicId{0}}, topology::NodeId{topology::NicId{destination}}};
}
[[nodiscard]] RouteRequest request(std::uint64_t flow = 0) {
    return {flow, endpoints(), transport::ByteCount{100}, transport::ByteCount{100}};
}
[[nodiscard]] std::vector<transport::DirectedLinkConfiguration>
configuration(const topology::TopologyGraph& graph) {
    std::vector<transport::DirectedLinkConfiguration> result;
    for (const auto& link : graph.links()) {
        if (link.kind == topology::LinkKind::Fabric) {
            for (const auto& arc : topology::directed_links(link)) {
                result.push_back({arc.id, transport::BitsPerSecond{8'000'000'000ULL},
                                  sim::SimDurationNs{25}, transport::ByteCount{1'000'000},
                                  std::nullopt});
            }
        }
    }
    return result;
}
class Dispatcher final {
  public:
    explicit Dispatcher(transport::TransportRuntime& runtime) : runtime_{runtime} {}
    std::function<void(const sim::NoOpEvent&, sim::SimulationContext&)> action;
    void operator()(const sim::NoOpEvent& event, sim::SimulationContext& context) const {
        action(event, context);
    }
    void operator()(const transport::ChunkArrivalEvent& e, sim::SimulationContext& c) {
        runtime_.handle_arrival(e, c);
    }
    void operator()(const transport::TransmissionCompleteEvent& e, sim::SimulationContext& c) {
        runtime_.handle_completion(e, c);
    }
    void operator()(const transport::LinkStateChangeEvent& e, sim::SimulationContext& c) {
        runtime_.handle_link_state_change(e, c);
    }
    void operator()(const transport::PortStateChangeEvent& e, sim::SimulationContext& c) {
        runtime_.handle_port_state_change(e, c);
    }
    void operator()(const transport::SwitchStateChangeEvent& e, sim::SimulationContext& c) {
        runtime_.handle_switch_state_change(e, c);
    }
    void operator()(const nexuslab::workload::WorkloadEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected workload event in this dispatcher"};
    }
    void operator()(const nexuslab::collective::LocalCompletionEvent& /*event*/,
                    nexuslab::sim::SimulationContext& /*context*/) const {
        throw std::logic_error{"unexpected collective event in this dispatcher"};
    }

  private:
    transport::TransportRuntime& runtime_;
};
[[nodiscard]] sim::SimulationResult run(Dispatcher& dispatcher) {
    sim::Simulation simulation{42};
    static_cast<void>(
        simulation.schedule({sim::SimTimeNs{0}, sim::EventPriority::Normal, sim::NoOpEvent{0}}));
    return simulation.run(dispatcher);
}
void check_path(const topology::TopologyGraph& graph, const Path& path, Endpoints pair) {
    auto current = pair.source;
    std::set<topology::NodeId> visited{current};
    for (const auto id : path) {
        const auto arcs = topology::directed_links(*graph.find(id.link));
        const auto arc = id.direction == topology::LinkDirection::AToB ? arcs[0] : arcs[1];
        equal(graph.find(arc.source)->owner, current);
        EXPECT_TRUE(graph.is_operational(arc));
        current = graph.find(arc.destination)->owner;
        EXPECT_TRUE(visited.insert(current).second);
    }
    equal(current, pair.destination);
}
TEST(RoutingPathsTest, EnumeratesAllCanonicalLoopFreeShortestClosPathsAtRequiredScales) {
    for (const std::size_t gpus : {128U, 512U, 2048U, 8192U}) {
        const auto graph = topology::generate_clos({gpus, 8, 8, 8});
        PathService service{*graph};
        const auto& paths = service.lookup(endpoints());
        equal(paths.size(), 8U);
        EXPECT_TRUE(std::ranges::is_sorted(paths));
        for (const auto& path : paths) {
            equal(path.size(), 4U);
            check_path(*graph, path, endpoints());
        }
        equal(service.lookup(endpoints(1)).size(), 1U);
        equal(service.lookup(endpoints(1)).front().size(), 2U);
    }
}
TEST(RoutingPathsTest, FindsLongerSurvivingPathInCyclicNonClosFabric) {
    topology::TopologyGraph graph;
    const auto a = topology::NodeId{graph.add_spine_switch()};
    const auto b = topology::NodeId{graph.add_spine_switch()};
    const auto c = topology::NodeId{graph.add_spine_switch()};
    static_cast<void>(graph.connect_fabric(a, topology::PortRole::FabricDownlink, b,
                                           topology::PortRole::FabricDownlink));
    static_cast<void>(graph.connect_fabric(b, topology::PortRole::FabricDownlink, c,
                                           topology::PortRole::FabricDownlink));
    const auto direct = graph.connect_fabric(a, topology::PortRole::FabricDownlink, c,
                                             topology::PortRole::FabricDownlink);
    PathService service{graph};
    equal(service.lookup({a, c}).front().size(), 1U);
    equal(graph.set_link_state(direct, topology::OperationalState::Down), true);
    const auto& longer = service.lookup({a, c});
    equal(longer.size(), 1U);
    equal(longer.front().size(), 2U);
    check_path(graph, longer.front(), {a, c});
    equal(graph.shortest_hops(a, c), longer.front().size());
    equal(graph.set_link_state(direct, topology::OperationalState::Up), true);
    equal(service.lookup({a, c}).front().size(), 1U);
}
TEST(RoutingPathsTest, DirectFabricExcludesGpuLocalAttachments) {
    const auto graph = topology::generate_two_gpu_direct();
    PathService paths{*graph};
    equal(paths.lookup(endpoints(1)).front().size(), 1U);
    EXPECT_TRUE(throws<std::invalid_argument>([&] {
        static_cast<void>(
            paths.lookup({topology::NodeId{topology::GpuId{0}}, endpoints(1).destination}));
    }));
}
TEST(RoutingPathsTest, RejectsUnknownIdenticalEndpointsAndExplicitEnumerationLimits) {
    const auto graph = topology::generate_clos({512, 8, 8, 8});
    PathService paths{*graph};
    EXPECT_TRUE(
        throws<std::invalid_argument>([&] { static_cast<void>(paths.lookup(endpoints(9999))); }));
    EXPECT_TRUE(
        throws<std::invalid_argument>([&] { static_cast<void>(paths.lookup(endpoints(0))); }));
    for (const auto limits :
         {PathLimits{7, 64, 10, 1000}, PathLimits{8, 3, 10, 1000}, PathLimits{8, 64, 10, 31}}) {
        PathService bounded{*graph, limits};
        EXPECT_TRUE(
            throws<std::length_error>([&] { static_cast<void>(bounded.lookup(endpoints())); }));
        equal(bounded.statistics().pairs, 0U);
    }
    EXPECT_TRUE(
        throws<std::invalid_argument>([&] { const PathService invalid{*graph, {0, 1, 1, 1}}; }));
}
TEST(RoutingPathsTest, FifoCacheIsBoundedAndReportsHits) {
    const auto graph = topology::generate_clos({512, 8, 8, 8});
    PathService paths{*graph, {64, 64, 1, 32}};
    static_cast<void>(paths.lookup(endpoints()));
    static_cast<void>(paths.lookup(endpoints()));
    equal(paths.statistics().hits, 1U);
    equal(paths.statistics().route_entries, 32U);
    static_cast<void>(paths.lookup(endpoints(1)));
    equal(paths.statistics().pairs, 1U);
    equal(paths.statistics().route_entries, 2U);
    static_cast<void>(paths.lookup(endpoints()));
    equal(paths.statistics().misses, 3U);
}
TEST(RoutingPathsTest, InvalidatesFailedLinkPortSwitchAndRecoveryWithoutManualNotification) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    PathService paths{*graph};
    const auto original = paths.lookup(endpoints());
    const auto link = original.front()[1].link;
    EXPECT_TRUE(graph->set_link_state(link, topology::OperationalState::Down));
    equal(paths.lookup(endpoints()).size(), 7U);
    EXPECT_TRUE(graph->set_link_state(link, topology::OperationalState::Up));
    equal(paths.lookup(endpoints()), original);
    const auto port = graph->find(link)->endpoint_b;
    EXPECT_TRUE(graph->set_port_state(port, topology::OperationalState::Down));
    equal(paths.lookup(endpoints()).size(), 7U);
    EXPECT_TRUE(graph->set_port_state(port, topology::OperationalState::Up));
    const auto spine = topology::SwitchId{graph->find(port)->owner.value()};
    EXPECT_TRUE(graph->set_switch_state(spine, topology::OperationalState::Down));
    equal(paths.lookup(endpoints()).size(), 7U);
    EXPECT_TRUE(graph->set_switch_state(spine, topology::OperationalState::Up));
    equal(paths.lookup(endpoints()), original);
    equal(paths.statistics().invalidations, 5U);
}
TEST(RoutingPathsTest, CachesNoPathAndRecoversAfterLeafFailure) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    PathService paths{*graph};
    EXPECT_TRUE(graph->set_switch_state(topology::SwitchId{0}, topology::OperationalState::Down));
    EXPECT_TRUE(paths.lookup(endpoints()).empty());
    EXPECT_TRUE(paths.lookup(endpoints()).empty());
    equal(paths.statistics().hits, 1U);
    EXPECT_TRUE(graph->set_switch_state(topology::SwitchId{0}, topology::OperationalState::Up));
    equal(paths.lookup(endpoints()).size(), 8U);
}
TEST(RoutingPathsTest, RevisionChangesOnlyForValidEffectiveStateChanges) {
    auto graph = topology::generate_two_gpu_direct();
    const auto id = graph->links().back().id;
    equal(graph->operational_revision(), 0U);
    EXPECT_TRUE(graph->set_link_state(id, topology::OperationalState::Up));
    EXPECT_FALSE(graph->set_link_state(id, topology::OperationalState{}));
    EXPECT_FALSE(
        graph->set_switch_state(topology::SwitchId{999}, topology::OperationalState::Down));
    equal(graph->operational_revision(), 0U);
    EXPECT_TRUE(graph->set_link_state(id, topology::OperationalState::Down));
    equal(graph->operational_revision(), 1U);
}
TEST(RoutingPolicyTest, RegistryRejectsInvalidConfigurationAndListsStableNames) {
    PolicyRegistry registry;
    equal(registry.names(),
          (std::vector<std::string>{"ecmp", "least-loaded", "queue-aware", "shortest-path"}));
    EXPECT_TRUE(throws<std::invalid_argument>([&] { static_cast<void>(registry.create("typo")); }));
    EXPECT_TRUE(
        throws<std::invalid_argument>([&] { registry.add("ecmp", [] { return nullptr; }); }));
    registry.add("null", [] { return nullptr; });
    EXPECT_TRUE(throws<std::invalid_argument>([&] { static_cast<void>(registry.create("null")); }));
}
TEST(RoutingPolicyTest, EcmpHasStableExplicitHashAndDistributesFlowKeys) {
    // Golden value is calculated independently from the specified little-endian encoding.
    equal(ecmp_hash(request(), 42), 14283379818690939014ULL);
    auto graph = topology::generate_clos({512, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    PathService paths{*graph};
    const auto& candidates = paths.lookup(endpoints());
    const auto policy = PolicyRegistry{}.create("ecmp");
    std::set<std::size_t> used;
    for (std::uint64_t flow = 0; flow < 1000; ++flow) {
        const auto req = request(flow);
        const PolicyInput input{req, 42, candidates, FabricView{runtime}};
        const auto choice = policy->choose(input);
        equal(choice, policy->choose(input));
        used.insert(choice.candidate);
    }
    equal(used.size(), 8U);
}
TEST(RoutingPolicyTest, IdleHomogeneousPathsUseCanonicalTies) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    PathService paths{*graph};
    const auto req = request();
    const PolicyInput input{req, 42, paths.lookup(req.endpoints), FabricView{runtime}};
    for (const auto* name : {"shortest-path", "least-loaded", "queue-aware"}) {
        equal(PolicyRegistry{}.create(name)->choose(input).candidate, 0U);
    }
    equal(PolicyRegistry{}.create("queue-aware")->choose(input).score, 500U);
    equal(sizeof(FabricView), sizeof(void*));
}
TEST(RoutingPolicyTest, QueueAwareAccountsBandwidthWhileLeastLoadedCountsBytes) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    PathService paths{*graph};
    const auto candidates = paths.lookup(endpoints());
    auto config = configuration(*graph);
    for (auto& arc : config) {
        if (arc.link == candidates[1][1]) {
            arc.bandwidth = transport::BitsPerSecond{8};
        }
    }
    transport::TransportRuntime runtime{*graph, config};
    Dispatcher dispatcher{runtime};
    dispatcher.action = [&](const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        runtime.register_chunk({{transport::TransferId{99}, transport::ChunkId{99},
                                 transport::ByteCount{100}, 0, false},
                                {candidates[0][1]}});
        static_cast<void>(runtime.schedule_initial_arrival(transport::ChunkId{99}, context));
    };
    // Observe after the first arrival using a separate bootstrap at t=1.
    const auto bootstrap = dispatcher.action;
    dispatcher.action = [&](const sim::NoOpEvent& event, sim::SimulationContext& context) {
        if (event.token == 0) {
            bootstrap(event, context);
            static_cast<void>(context.schedule(
                {sim::SimTimeNs{1}, sim::EventPriority::Normal, sim::NoOpEvent{1}}));
        } else {
            const auto req = request();
            const std::vector<Path> two{candidates[0], candidates[1]};
            const PolicyInput input{req, 42, two, FabricView{runtime}};
            equal(PolicyRegistry{}.create("least-loaded")->choose(input).candidate, 1U);
            equal(PolicyRegistry{}.create("queue-aware")->choose(input).candidate, 0U);
            equal(FabricView{runtime}.read(candidates[0][1]).outstanding_bytes.value(), 100U);
        }
    };
    equal(run(dispatcher).status, sim::SimulationStatus::Completed);
}
TEST(RoutingPolicyTest, RejectsUnconfiguredArcAndOverflowingDelay) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    PathService paths{*graph};
    const auto candidates = paths.lookup(endpoints());
    auto config = configuration(*graph);
    for (auto& arc : config) {
        arc.propagation_delay = sim::SimDurationNs{std::numeric_limits<std::uint64_t>::max()};
    }
    transport::TransportRuntime runtime{*graph, config};
    const auto req = request();
    EXPECT_TRUE(throws<std::overflow_error>([&] {
        static_cast<void>(PolicyRegistry{}
                              .create("queue-aware")
                              ->choose({req, 42, candidates, FabricView{runtime}}));
    }));
    EXPECT_TRUE(throws<std::invalid_argument>([&] {
        static_cast<void>(
            FabricView{runtime}.read({topology::LinkId{99999}, topology::LinkDirection::AToB}));
    }));
}
TEST(RouterTest, RecordsSubmissionIdentityAndDrainsExactlyOnce) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    Router router{*graph, runtime, PolicyRegistry{}, {"shortest-path"}};
    Dispatcher dispatcher{runtime};
    dispatcher.action = [&](const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        EXPECT_TRUE(router.submit(request(), context).has_value());
    };
    equal(run(dispatcher).status, sim::SimulationStatus::Completed);
    const auto records = router.take_decisions();
    equal(records.size(), 1U);
    equal(records.front().request, request());
    equal(records.front().transfer, transport::TransferId{0});
    equal(records.front().candidates, 8U);
    equal(records.front().timestamp, sim::SimTimeNs{0});
    equal(runtime.take_completed_transfers().front().outcome,
          transport::TransferOutcome::Succeeded);
    EXPECT_TRUE(router.take_decisions().empty());
}
TEST(RouterTest, DisconnectionRecordsNoRouteWithoutCreatingTransfer) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    EXPECT_TRUE(graph->set_switch_state(topology::SwitchId{0}, topology::OperationalState::Down));
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    Router router{*graph, runtime, PolicyRegistry{}};
    Dispatcher dispatcher{runtime};
    dispatcher.action = [&](const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        EXPECT_FALSE(router.submit(request(), context).has_value());
    };
    equal(run(dispatcher).status, sim::SimulationStatus::Completed);
    equal(router.decisions().size(), 1U);
    EXPECT_TRUE(router.decisions().front().path.empty());
    EXPECT_FALSE(runtime.transfer_snapshot(transport::TransferId{0}).has_value());
}
TEST(RouterTest, BoundedDecisionBufferRejectsBeforeAdmissionThenCanBeDrained) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    Router router{*graph, runtime, PolicyRegistry{}, {"ecmp", 42, {}, 1}};
    Dispatcher dispatcher{runtime};
    dispatcher.action = [&](const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        static_cast<void>(router.submit(request(), context));
        EXPECT_TRUE(throws<std::length_error>(
            [&] { static_cast<void>(router.submit(request(1), context)); }));
        EXPECT_FALSE(runtime.transfer_snapshot(transport::TransferId{1}).has_value());
        equal(router.take_decisions().size(), 1U);
        equal(router.submit(request(1), context).has_value(), true);
    };
    equal(run(dispatcher).status, sim::SimulationStatus::Completed);
}
TEST(RouterTest, FailureReroutesNewAdmissionsForAllComponentTypesAndPreservesOldLoss) {
    for (int component = 0; component < 3; ++component) {
        auto graph = topology::generate_clos({512, 8, 8, 8});
        transport::TransportRuntime runtime{*graph, configuration(*graph)};
        Router router{*graph, runtime, PolicyRegistry{}, {"shortest-path"}};
        Dispatcher dispatcher{runtime};
        dispatcher.action = [&](const sim::NoOpEvent& event, sim::SimulationContext& context) {
            if (event.token == 0) {
                static_cast<void>(router.submit(request(), context));
                const auto link = router.decisions().front().path[1].link;
                const auto port = graph->find(link)->endpoint_b;
                if (component == 0) {
                    static_cast<void>(runtime.schedule_link_state_change(
                        link, topology::OperationalState::Down, sim::SimTimeNs{40}, context));
                } else if (component == 1) {
                    static_cast<void>(runtime.schedule_port_state_change(
                        port, topology::OperationalState::Down, sim::SimTimeNs{40}, context));
                } else {
                    static_cast<void>(runtime.schedule_switch_state_change(
                        topology::SwitchId{graph->find(port)->owner.value()},
                        topology::OperationalState::Down, sim::SimTimeNs{40}, context));
                }
                static_cast<void>(context.schedule(
                    {sim::SimTimeNs{60}, sim::EventPriority::Normal, sim::NoOpEvent{1}}));
            } else {
                equal(router.submit(request(1), context).has_value(), true);
                check_path(*graph, router.decisions().back().path, endpoints());
                equal(router.decisions().front().path == router.decisions().back().path, false);
            }
        };
        equal(run(dispatcher).status, sim::SimulationStatus::Completed);
        const auto outcomes = runtime.take_completed_transfers();
        equal(outcomes.size(), 2U);
        equal(outcomes.front().outcome, transport::TransferOutcome::Failed);
        equal(outcomes.back().outcome, transport::TransferOutcome::Succeeded);
        equal(router.cache_statistics().invalidations, 1U);
    }
}

class InvalidPolicy final : public RoutingPolicy {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "invalid-index"; }
    [[nodiscard]] std::uint64_t version() const noexcept override { return 1; }
    [[nodiscard]] RouteChoice choose(const PolicyInput& /*input*/) const override {
        return {999, 0, "invalid plugin result"};
    }
};
TEST(RouterTest, RejectsPluginOffCandidatePathBeforeTransportMutation) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    PolicyRegistry registry;
    registry.add("invalid-index", [] { return std::make_unique<InvalidPolicy>(); });
    Router router{*graph, runtime, registry, {"invalid-index"}};
    Dispatcher dispatcher{runtime};
    dispatcher.action = [&](const sim::NoOpEvent& /*event*/, sim::SimulationContext& context) {
        EXPECT_TRUE(throws<std::invalid_argument>(
            [&] { static_cast<void>(router.submit(request(), context)); }));
        EXPECT_FALSE(runtime.transfer_snapshot(transport::TransferId{0}).has_value());
        EXPECT_TRUE(router.decisions().empty());
    };
    equal(run(dispatcher).status, sim::SimulationStatus::Completed);
}
TEST(RouterTest, RepeatsCompleteDecisionRecordsAndOutcomesForEveryPolicy) {
    for (const auto& name : PolicyRegistry{}.names()) {
        std::vector<RouteDecision> expected_decisions;
        std::vector<transport::TransferCompletion> expected_outcomes;
        for (int repetition = 0; repetition < 3; ++repetition) {
            auto graph = topology::generate_clos({512, 8, 8, 8});
            transport::TransportRuntime runtime{*graph, configuration(*graph)};
            Router router{*graph, runtime, PolicyRegistry{}, {name}};
            Dispatcher dispatcher{runtime};
            dispatcher.action = [&](const sim::NoOpEvent& event, sim::SimulationContext& context) {
                static_cast<void>(router.submit(request(event.token), context));
                if (event.token < 15) {
                    static_cast<void>(context.schedule({sim::SimTimeNs{(event.token + 1) * 20},
                                                        sim::EventPriority::Normal,
                                                        sim::NoOpEvent{event.token + 1}}));
                }
            };
            equal(run(dispatcher).status, sim::SimulationStatus::Completed);
            const auto decisions = router.take_decisions();
            const auto outcomes = runtime.take_completed_transfers();
            equal(decisions.size(), 16U);
            equal(outcomes.size(), 16U);
            if (repetition == 0) {
                expected_decisions = decisions;
                expected_outcomes = outcomes;
            }
            equal(decisions, expected_decisions);
            equal(outcomes, expected_outcomes);
        }
    }
}
TEST(RoutingPolicyTest, QueueViewIncludesWaitingAndFullActiveChunkWithoutCopyingFabric) {
    auto graph = topology::generate_clos({512, 8, 8, 8});
    transport::TransportRuntime runtime{*graph, configuration(*graph)};
    PathService paths{*graph};
    const auto path = paths.lookup(endpoints()).front();
    Dispatcher dispatcher{runtime};
    dispatcher.action = [&](const sim::NoOpEvent& event, sim::SimulationContext& context) {
        if (event.token == 0) {
            static_cast<void>(runtime.submit_transfer({endpoints().source, endpoints().destination,
                                                       transport::ByteCount{250},
                                                       transport::ByteCount{100}, path},
                                                      context));
            static_cast<void>(context.schedule(
                {sim::SimTimeNs{50}, sim::EventPriority::Normal, sim::NoOpEvent{1}}));
        } else {
            equal(FabricView{runtime}.read(path.front()).outstanding_bytes.value(), 250U);
            equal(FabricView{runtime}.read(path[1]).outstanding_bytes.value(), 0U);
        }
    };
    equal(run(dispatcher).status, sim::SimulationStatus::Completed);
}
} // namespace
} // namespace nexuslab::routing
