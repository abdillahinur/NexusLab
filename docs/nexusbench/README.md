<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusBench Planning Specification

Status: Planned — no benchmark pack, implementation, results, or gate approval exists yet.

NexusLab is the deterministic counterfactual experimentation lab. NexusBench is the versioned open
benchmark and research standard built on its scenario, policy, metric, repeat, and digest contracts.
The master source of truth is [NEXUSLAB_MASTER_PLAN.md](../../NEXUSLAB_MASTER_PLAN.md), especially
Sections 16A, 23, and 25.

## NexusBench-v0 planned scope

V0 is synthetic-only and intentionally limited to:

- NB-Routing-v0: ECMP versus queue-aware under frozen incast and spine-link-failure scenarios;
- NB-Placement-v0: first-fit versus compact under frozen rack-pressure and multi-tenant scenarios.

Every included track must have at least two baselines and a complete three-repeat matrix. The repeat
count is a harness contract, not an automatic statistical-significance claim.

## Specification outline

Before implementation, ADR-012 and ADR-013 must define:

1. suite manifest, scenario-pack layout, version compatibility, and evolution rules;
2. canonical suite/scenario, seed, policy artifact/configuration, simulator, metric-catalog, and
   outcome digest scopes;
3. static C++20 policy submission boundary and conformance tests;
4. primary/secondary metric dictionary, units, aggregations, missing/failed-cell handling, and
   caveat fields;
5. repository-native JSON/Markdown result and leaderboard format;
6. citation receipt and clean-clone reproduction instructions;
7. synthetic-result labeling and forbidden predictive/hyperscaler claims.

## Planned repository shape

```text
nexusbench/
└── v0/
    ├── suite.yaml
    ├── metrics.yaml
    ├── routing/
    └── placement/

docs/nexusbench/
├── README.md
├── specification-v0.md
├── metrics-v0.md
├── policy-submissions.md
└── citation.md
```

Only Cluster 24 may introduce the executable pack and final paths after its ADRs are accepted. This
outline is not evidence that those files, commands, or results exist.

## Required honesty

NexusBench-v0 will compare policies under locked synthetic models. It will not claim perfect
hardware fidelity, NCCL/RDMA equivalence, real-cluster prediction, production actuation, or
hyperscaler validation. No measured value or ranking belongs in planning documentation.
