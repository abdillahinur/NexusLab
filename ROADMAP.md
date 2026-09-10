<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab / NexusBench Roadmap

The detailed scope, acceptance criteria, and gates are defined in [NEXUSLAB_MASTER_PLAN.md](NEXUSLAB_MASTER_PLAN.md). This file is the concise execution view.

NexusLab is the deterministic counterfactual experimentation lab. NexusBench is its versioned open
evaluation standard. Supporting capabilities are sequenced by the benchmark tracks and adoption
stages they unlock, not as an independent feature checklist.

| Milestone | Clusters | Outcome | Status |
|---|---|---|---|
| Foundation | 0 | Reproducible C++ build, tests, CI, tooling, and documentation | Complete — gate approved |
| Simulation kernel | 1 | Deterministic event engine and measured baseline | Complete — gate approved |
| Fabric MVP | 2–3 | Clos topology plus chunk-level links, queues, and transfers | Complete; Clusters 2 and 3 gates approved |
| Routing comparison | 4 | ECMP, least-loaded, and queue-aware routing | Complete; Cluster 4 gate approved |
| Training workload MVP | 5–6 | Synthetic jobs and Ring AllReduce | Complete; Clusters 5 and 6 gates approved |
| Multi-tenant cluster | 7 | First-fit scheduling and placement comparisons | Complete; Cluster 7 gate approved |
| Failures and telemetry | 8–9 | Observable spine-link failure and recovery | Cluster 8 complete/gate approved; Cluster 9 next |
| Experiment platform | 11–12 | Reproducible matrices, manifests, digests, results, and replay | Not started |
| NexusBench-v0 specification and harness | 24 + minimum 14–15 | Frozen routing/placement tracks, two baselines each, three-repeat matrices, metrics, policy guide, and digests | Not started; ADR-012 and ADR-013 required |
| Advanced parallelism | 20 | DP/TP/PP/EP models feeding the planned NB-Collective track | Not started; ADR-014 required |
| Heterogeneous multi-rail fabric | 21 | Rail/domain-aware routing, placement, collective, and recovery scenarios | Not started; ADR-015 required |
| Trace-driven replay | 22 | Canonical ingest and same-workload-digest counterfactual track; Adoption Stage 1 | Not started; ADR-016 required |
| Flagship NexusBench evidence | 23 + 14 | First reference study with receipts and attribution/counterfactual deltas | Not started |
| Production-shaped shadow boundary | 16 | Same policy artifact across sim/replay/mock shadow; non-applied audit; Adoption Stage 2 | Not started; ADR-017 required |
| Analysis and later calibration | 14 | NexusBench attribution now; held-out ranking validity at Adoption Stage 3 later | Not started; ADR-018 required for calibration |
| NexusBench analysis dashboard | 13 | Replay-only explanation of NexusBench comparisons and approved evidence | Not started; sequenced after NexusBench-v0, traces, shadow, and attribution |
| Advanced controllers | 10 | Optional congestion-control experiments | Not started; does not outrank required scientific evidence |

Clusters 14, 15, 17, 18, and 19 provide cross-cutting reporting, extensibility, performance, testing,
and documentation work at the points defined by the master plan. The minimum Cluster 14 report path
and Cluster 15 static policy SDK are dependencies of Cluster 24; deeper attribution supports Cluster
23, while ranking calibration remains later Stage 3 work.

## Scientific delivery order

After Clusters 8–9, implement the necessary Clusters 11–12 experiment foundation; define the
minimum Cluster 15 policy SDK and Cluster 14 reporting path; then gate Cluster 24 NexusBench-v0.
Continue with Clusters 20, 21, 22, 23, 16, and deeper Cluster 14 attribution, followed by Cluster 13.
Only one implementation cluster is active at a time. The thin MVP remains releasable earlier;
“research-ready” requires verified NexusBench-v0 artifacts, while scientific-release completion
additionally requires the supporting capability gates and one measured reference study.

## NexusBench-v0 and adoption ladder

- NexusBench-v0 includes NB-Routing-v0 (ECMP versus queue-aware under incast/spine failure) and
  NB-Placement-v0 (first-fit versus compact under rack pressure/multi-tenant interference).
- Stage 0: frozen synthetic NexusBench-v0 and baselines.
- Stage 1: trace-driven counterfactual through the canonical intermediate schema.
- Stage 2: mock shadow adapter and complete `applied=false` audit.
- Stage 3: design-partner held-out policy-ranking calibration; this is the major trust unlock.
- Stage 4: human-reviewed advisory recommendations mapped to operator-facing knobs.

No stage permits production actuation, and honesty/data-governance requirements apply throughout.

## Delivery policy

- Scientific-release planning target: 16 weeks.
- Architecture gates are quality requirements, not deadlines.
- Only one implementation cluster may be active at a time.
- No benchmark or product claim may use fabricated or placeholder results.
- NexusBench-v0 scenarios, seeds, metrics, versions, and digest rules are frozen and changed only
  through explicit suite evolution.
- Imported traces are approximate behavioral inputs, not bit-exact NCCL/RDMA replay.
- Scientific-release shadow mode is read-only and records `applied=false`; real control and auto-remediation
  are out of scope.
- The 2,000- and 10,000-GPU what-if classes and overnight matrices are future measured goals, not
  achieved scale claims.
