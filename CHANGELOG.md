<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Changelog

All notable changes to NexusLab will be documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project will use semantic versioning once public releases begin.

## [Unreleased]

### Added

- Cluster 0 repository foundation.
- Apache License 2.0 licensing.
- Initial architecture decisions and roadmap.
- Cluster 1 deterministic event semantics and lifecycle architecture.
- Strong simulated-time and event identifier types with checked time arithmetic.
- Deterministic random-number generation with stable bounded-integer sampling.
- Typed value events with deterministic timestamp, priority, and event-ID queue ordering.
- Single-use simulation lifecycle with scheduling, typed dispatch, causality, cancellation, stop, and
  failure results.
- Typed simulation traces with explicit stable encoding, optional collection, and a deterministic
  64-bit FNV-1a digest.
- Trace-disabled simulation benchmark reporting insertion and dispatch throughput, event size, and
  resident memory.
- Initial one-million and ten-million-event simulation-core performance and memory baseline.
- Cluster 1 architecture gate approval with initial post-baseline local regression guardrails.
- Cluster 2 topology identity, directionality, Clos generation, validation, and serialization
  architecture.
- Strong topology IDs and foundational GPU, NIC, switch, rack, port, and directed-link value types.
- Validated topology construction with dense lookup, automatic local attachments, fabric links, and
  deterministic directed adjacency.
- Operational link, port, and switch failures with recovery-safe shortest-hop reachability.
- Structured whole-topology validation for identity, ownership, relationships, links, adjacency,
  and connectivity.
- Deterministic two-tier Clos generation for the approved 512-GPU initial and 2,048-GPU stretch
  profiles, including equal-cost shortest-path counts.
- Versioned canonical topology YAML serialization and deterministic Graphviz DOT export.
- Strict topology YAML loading with validated reconstruction and byte-stable canonical round trips.
- Inspection-only topology summary CLI for approved Clos profiles and canonical YAML files.
- Deterministic two-GPU direct, single-rack, and configurable leaf-spine topology generators.
- Clos-specific structural validation, required-scale coverage through 8,192 GPUs, and a topology
  benchmark harness.
- Initial Cluster 2 topology performance, memory, and serialization baseline across all required
  scales.
- Linear-time topology port-occupancy validation replacing repeated full-link scans.
- Measured Cluster 2 validation optimization results, including an 87.46x speedup at 8,192 GPUs.
- Cluster 2 architecture gate approval with initial post-baseline local regression guardrails.
- Cluster 3 deterministic link, FIFO queue, chunk transfer, timing, drop, and failure semantics.
