<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusLab Roadmap

The detailed scope, acceptance criteria, and gates are defined in [NEXUSLAB_MASTER_PLAN.md](NEXUSLAB_MASTER_PLAN.md). This file is the concise execution view.

| Milestone | Clusters | Outcome | Status |
|---|---|---|---|
| Foundation | 0 | Reproducible C++ build, tests, CI, tooling, and documentation | Complete — gate approved |
| Simulation kernel | 1 | Deterministic event engine and measured baseline | Complete — gate approved |
| Fabric MVP | 2–3 | Clos topology plus chunk-level links, queues, and transfers | Complete; Clusters 2 and 3 gates approved |
| Routing comparison | 4 | ECMP, least-loaded, and queue-aware routing | Not started |
| Training workload MVP | 5–6 | Synthetic jobs and Ring AllReduce | Not started |
| Multi-tenant cluster | 7 | First-fit scheduling and placement comparisons | Not started |
| Failures and telemetry | 8–9 | Observable spine-link failure and recovery | Not started |
| Experiment platform | 11–12 | Reproducible matrices, results, and replay | Not started |
| Portfolio dashboard | 13 | Replay-only explanatory dashboard | Not started |
| Advanced controllers | 10 | Congestion-control experiments | Not started |
| Real-cluster readiness | 16 | Backend abstraction and mock shadow mode | Not started |

Clusters 14, 15, 17, 18, and 19 provide cross-cutting reporting, extensibility, performance, testing, and documentation work at the points defined by the master plan.

## Delivery policy

- Portfolio-ready MVP target: 16 weeks.
- Architecture gates are quality requirements, not deadlines.
- Only one implementation cluster may be active at a time.
- No benchmark or product claim may use fabricated or placeholder results.
