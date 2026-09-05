<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# ADR-009 — Ring AllReduce planning and execution

Date: 2026-09-05. Status: Accepted; validated by Architecture Gate 6.

Use a bandwidth-only synthetic Ring AllReduce: P ordered unique GPU participants, P shards with
quotient/remainder byte partitioning, P−1 reduce-scatter rounds followed by P−1 all-gather rounds.
In reduce-scatter round r, rank i sends shard (i+P−r)%P to (i+1)%P; after reduction rank i owns
shard (i+1)%P. In all-gather round r it sends (i+1+P−r)%P. Every round waits for all nonempty
transfers before the next round begins. Zero-byte shards issue no transfer. A one-worker collective
completes immediately with zero communication. Total planned logical bytes are 2(P−1)×gradient.

The planner is pure and computes one round at a time, avoiding O(P²) retained plans. Rank ordering
is supplied explicitly and is not topology-aware. Routing is selected by the existing Router per
remote round transfer; the collective engine does not implement a routing policy. Reduction data
values, arithmetic execution cost, channels, pipelined cross-round forwarding and NCCL fidelity are
not claimed. Global round barriers are conservative relative to an aggressively pipelined ring.

GPUs sharing a NIC use an explicit synthetic local-link bandwidth and latency, scheduled with a
typed local-completion event. Local transfers have independent service in this initial model;
there is no contention model for PCIe/NVLink/memory or competition with GPU compute. Remote GPUs
map to their attached NICs and use existing chunked fabric transport. Local and fabric byte counts
remain separate; neither silently treats same-NIC communication as a zero-hop fabric transfer.

The executor records phase/round progress, issued/delivered local and fabric bytes, completion
outcomes, and timestamped round transitions. A failed route or transfer stops future rounds; the
current round drains before the collective emits exactly one failed result. Cancellation also
stops new rounds but lets already-issued work drain. Cancelled jobs can be terminal before their
orphaned collective traffic finishes. No implicit retry is added.

The composition dispatcher owns transport completion consumption. Unknown transfer identities
are errors, not discarded results. The workload layer maps collective IDs to job/step/bucket and
advances only on successful completion. It does not embed ring rounds. Retained collective and
participant counts, pending round operations, and timeline records are explicitly bounded.
Allocation or event-ID exhaustion is fatal with no rollback guarantee.
