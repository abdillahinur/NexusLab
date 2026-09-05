<!--
SPDX-FileCopyrightText: 2026 NexusLab contributors
SPDX-License-Identifier: Apache-2.0
-->

# Synthetic training scenarios

Build with `bash scripts/build.sh release`, then run:

```bash
build/release/simulator/nexuslab train --file examples/training/two-worker.yaml --timeline
build/release/simulator/nexuslab train --file examples/training/overlap-straggler.yaml --timeline
build/release/simulator/nexuslab train --profiles
```

If `NEXUSLAB_BUILD_ROOT` is set, use its `release/simulator/nexuslab` executable instead.
The first example is a complete compute → Ring AllReduce → barrier → next-step run. The second
runs two explicitly assigned jobs with staggered arrivals and an overlapped straggler workload.
This is a synthetic simulator, not execution of an ML model or a hardware-calibrated predictor.

## Version 1 schema

Unknown and duplicate keys are rejected. Scenario files are limited to one MiB. Decimal integer
scalars are unsigned and at most 20 characters. There are at most 10,000 jobs and controls,
8,192 workers per job, one million total worker entries, and 256 bytes per job name.

| Root field | Meaning / default |
|---|---|
| `version` | Required; exactly `1` |
| `gpus` | Synthetic Clos GPU count, multiple of 64 from 64 to 8,192; default 64 |
| `seed` | Deterministic simulation/routing seed, default 42 |
| `routing_policy` | `ecmp` (default), `shortest-path`, `least-loaded`, or `queue-aware` |
| `bandwidth_bps` | Fabric bandwidth, default 100,000,000,000; must be positive |
| `propagation_ns` | Fabric per-hop propagation, default 500 |
| `buffer_bytes` | Waiting buffer per directed fabric arc, default 262,144; zero exercises loss |
| `local_bandwidth_bps` | Independent local transfer bandwidth, default 800,000,000,000 |
| `local_latency_ns` | Local transfer latency, default 0 |
| `jobs` | Required nonempty sequence of job mappings |
| `controls` | Optional job cancellation/worker-failure event sequence |

Topology uses eight GPUs per NIC and eight NICs per rack with eight spines. Thus GPU 0 and GPU 1
share a NIC; GPU 0 and GPU 8 communicate through the fabric. These are explicit synthetic local
and fabric parameters, not vendor defaults.

| Job field | Meaning / default |
|---|---|
| `name` | Human label; defaults to selected profile name |
| `profile` | Parameter template; default `small-data-parallel` |
| `workers` | Required ordered list of unique existing GPU IDs; order defines the ring |
| `arrival_ns` | Absolute arrival, default 0 |
| `steps` | Positive step count, from profile |
| `compute_ns` | Positive scalar for every worker or a duration list matching `workers`; from profile |
| `gradient_bytes` | Positive total bytes per step, from profile |
| `bucket_bytes` | Positive maximum bucket size; last bucket may be smaller; from profile |
| `chunk_bytes` | Positive fabric transfer chunk size, default 4,096 |
| `overlap` | Exactly `true` or `false`; default false |
| `priority` | Unsigned metadata for future scheduling; it does not alter current admission order |
| `collective` | Only `allreduce`, the default |
| `algorithm` | Only `ring`, the default |

Assignments are exclusive while a job runs. An overlap in GPU assignments fails the arriving job;
there is no automatic placement, waiting queue or preemption. Simultaneous arrivals follow stable
event-ID/input order. Disjoint jobs can run concurrently and contend on shared fabric links.

Without overlap, all worker compute finishes before the first bucket's collective. With overlap,
compute is divided into equal-count bucket readiness intervals (integer rounding upward), and each
bucket waits for every worker and the preceding bucket collective. Compute remains continuous;
communication does not slow it down. Gradient-size remainders affect bucket bytes, not the equal-count
compute timing model. The next step starts only after every bucket succeeds.

A control mapping contains `job` (zero-based job index), `kind` (`cancel` or `worker_failure`),
`at_ns` (absolute time), and optional `worker` (zero-based rank, default 0). Controls have critical
priority, ahead of compute completions at the same time. Worker failure aborts this job; it does not
permanently disable that physical GPU. Future jobs may reuse released assignments.

```yaml
controls:
  - job: 0
    kind: worker_failure
    worker: 1
    at_ns: 15000
```

Cancellation/failure cancels pending compute and prevents new collective rounds. Already-issued
network/local transfers drain. Therefore the simulator's final timestamp can exceed the cancelled
job's completion timestamp. No retries or reliable recovery are implied.

## Metrics and timelines

The CLI exits 0 when every job succeeds, 1 when a job fails/cancels or a run error occurs, and 2 for
invalid command syntax. A drained event queue alone is not a successful workload outcome.

- `elapsed_ns`: arrival-to-terminal job duration; pre-arrival cancellation has zero elapsed work.
- `compute_gpu_ns`: sum of actual compute intervals over assigned workers, including partial work.
- `idle_gpu_ns`: allocated GPU time minus compute time; includes straggler/barrier and communication
  waits. Overlap never double-counts compute. Assignment rejection allocates no GPU time.
- `maximum_waiting_bytes`: maximum observed waiting occupancy among directed fabric queues.
- `job_event`: timestamped step compute start/end, bucket collective start/end and terminal state.
- `collective=` links a job/bucket timeline record to the collective timeline ID.
- `collective_event`: reduce-scatter/all-gather round starts and terminal collective outcome.

Collective API results also expose planned logical bytes, issued fabric/local bytes, delivered bytes,
and start/finish times. Successful P-worker Ring AllReduce sends exactly `2(P−1)×gradient_bytes`.
Quotient/remainder shards support uneven gradients and zero-sized shards; zero-sized sends are
skipped. Single-worker AllReduce completes with zero communication. Runtime transfers model bytes,
not tensor contents; reduction arithmetic is not timed.

## Profiles and limits

`train --profiles` lists seven deterministic templates: small data-parallel, large LLM,
communication-heavy, compute-heavy, bursty MoE, checkpoint-heavy, and inference-burst proxies.
MoE has no expert dispatch model, checkpoints have no storage engine, and inference has no token
model. Their names describe compute/communication parameter approximations only. Explicit job fields
override a template. No stochastic arrival generator or production workload calibration is claimed.

Defaults bound retained job state to one million worker entries and one million compute-event
slots, at most 4,096 buckets per step, and one million timeline entries. The ring executor retains
at most 100,000 collectives, one million participant entries and one million timeline records.
Transport and routing retain their existing chunk/route/decision limits. Large combinations of
profile, workers, steps and chunk size may exceed those limits; reduce the scenario or explicitly
configure API budgets. Input validation rejects ordinary invalid dimensions; resource exhaustion
while running is fatal and does not support rollback/resumption. Completed state remains inspectable
until the run is destroyed. Durable replay and telemetry storage are later milestones.

Local transfers share neither a serialized local bus nor a contention model. Each uses its configured
bandwidth/latency independently. Ring rounds use global barriers; pipelined channels, reduction
arithmetic, topology-aware rank ordering and actual NCCL behavior are outside this model.
