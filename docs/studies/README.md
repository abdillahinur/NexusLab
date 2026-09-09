<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# NexusBench Study Artifacts

Status: Planned — no reference study is complete and this directory contains no measured result.

Verified NexusBench case studies will use `docs/studies/<study-id>.md`. Cluster 23 owns the first
reference study, and Cluster 14 owns reproducible reporting and attribution. A study document must
not be populated with measurements until all referenced runs are complete and digest verification
passes.

## Required study structure

```md
# <Study title>

Status: Complete only after the evidence gate passes

## Question and predeclared hypothesis
## NexusBench track and suite version
## Locked manifest and comparison cells
## Policies and versions
## Scenario, workload/trace, failure, seed, simulator, and metric digests
## Reproducible commands
## Run receipts and artifact locations
## Results generated from saved runs
## Attribution and counterfactual deltas
## Failed, missing, neutral, and adverse outcomes
## Limitations and allowed claim
```

Placeholders, invented values, hand-edited charts, and selective seed removal are forbidden.
