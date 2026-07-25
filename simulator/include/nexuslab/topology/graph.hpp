// SPDX-FileCopyrightText: 2026 NexusLab contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "nexuslab/topology/entities.hpp"
#include "nexuslab/topology/id.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace nexuslab::topology {

class TopologyGraph final {
  public:
    [[nodiscard]] RackId add_rack();
    [[nodiscard]] NicId add_nic(RackId rack);
    [[nodiscard]] GpuId add_gpu(NicId attached_nic);
    [[nodiscard]] SwitchId add_leaf_switch(RackId rack);
    [[nodiscard]] SwitchId add_spine_switch();
    [[nodiscard]] LinkId connect_fabric(NodeId endpoint_a, PortRole role_a, NodeId endpoint_b,
                                        PortRole role_b);

    [[nodiscard]] bool set_link_state(LinkId id, OperationalState state) noexcept;
    [[nodiscard]] bool set_port_state(PortId id, OperationalState state) noexcept;
    [[nodiscard]] bool set_switch_state(SwitchId id, OperationalState state) noexcept;

    [[nodiscard]] const GpuWorker* find(GpuId id) const noexcept;
    [[nodiscard]] const Nic* find(NicId id) const noexcept;
    [[nodiscard]] const Switch* find(SwitchId id) const noexcept;
    [[nodiscard]] const Rack* find(RackId id) const noexcept;
    [[nodiscard]] const Port* find(PortId id) const noexcept;
    [[nodiscard]] const PhysicalLink* find(LinkId id) const noexcept;

    [[nodiscard]] std::span<const GpuWorker> gpus() const noexcept;
    [[nodiscard]] std::span<const Nic> nics() const noexcept;
    [[nodiscard]] std::span<const Switch> switches() const noexcept;
    [[nodiscard]] std::span<const Rack> racks() const noexcept;
    [[nodiscard]] std::span<const Port> ports() const noexcept;
    [[nodiscard]] std::span<const PhysicalLink> links() const noexcept;
    [[nodiscard]] std::span<const DirectedLink> outgoing(NodeId node) const;
    [[nodiscard]] bool is_operational(const DirectedLink& link) const noexcept;
    [[nodiscard]] std::optional<std::size_t> shortest_hops(NodeId source, NodeId destination) const;
    [[nodiscard]] bool reachable(NodeId source, NodeId destination) const;

  private:
    [[nodiscard]] bool contains(NodeId node) const noexcept;
    [[nodiscard]] bool node_is_operational(NodeId node) const noexcept;
    [[nodiscard]] bool directly_connected(NodeId endpoint_a, NodeId endpoint_b) const;
    [[nodiscard]] std::vector<DirectedLink>& mutable_adjacency(NodeId node);
    [[nodiscard]] const std::vector<DirectedLink>& adjacency(NodeId node) const;
    void append_owner_port(NodeId owner, PortId port);
    void append_link(PhysicalLink link, NodeId endpoint_a, NodeId endpoint_b);

    SequentialIdGenerator<GpuId> gpu_ids_;
    SequentialIdGenerator<NicId> nic_ids_;
    SequentialIdGenerator<SwitchId> switch_ids_;
    SequentialIdGenerator<RackId> rack_ids_;
    SequentialIdGenerator<PortId> port_ids_;
    SequentialIdGenerator<LinkId> link_ids_;
    std::vector<GpuWorker> gpus_;
    std::vector<Nic> nics_;
    std::vector<Switch> switches_;
    std::vector<Rack> racks_;
    std::vector<Port> ports_;
    std::vector<PhysicalLink> links_;
    std::vector<std::vector<DirectedLink>> gpu_adjacency_;
    std::vector<std::vector<DirectedLink>> nic_adjacency_;
    std::vector<std::vector<DirectedLink>> switch_adjacency_;
};

} // namespace nexuslab::topology
