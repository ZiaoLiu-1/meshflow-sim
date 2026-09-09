# Public architecture correspondence

MeshFlow uses local state and neighbor communication to make a small simulator inspectable. The correspondence below is conceptual; its instruction set, FIFO rules, latencies, and deterministic ordering are defined by [our SPEC](../SPEC.md).

Public sources were reopened on **2026-09-09 UTC / 2026-09-08 America/Toronto**. They are references, not dependencies or evidence that this repository ran Cerebras software.

| Concept | Public source | MeshFlow correspondence and simplification |
| --- | --- | --- |
| PE and local memory | Cerebras describes independent PEs with their own memory, PC, and code in a two-dimensional mesh. Other PEs do not directly access a PE's memory. [Architecture](https://sdk.cerebras.ai/computing-with-cerebras) | `PEState` owns registers, PC, and local integer memory. The topology is a one-dimensional chain; there is no shared-memory instruction. |
| Routes and data movement | The two-PE GEMV example sends the left PE's partial result to the right using fabric DSDs and configured routes. [Routes and Fabric DSDs](https://sdk.cerebras.ai/csl/tutorials/gemv-06-routes-1) | Explicit SEND/RECV name an adjacent PE. Each direction has one FIFO; there are no colors, router configuration, DSDs, or multidimensional paths. |
| Task activation | Cerebras tasks become runnable through activation and unblocking, including activation associated with arriving wavelets. [Architecture](https://sdk.cerebras.ai/computing-with-cerebras) | An empty RECV waits until a message is visible. This is only a readiness analogy: the model has one instruction stream per PE and no CSL task IDs or task picker. |
| Buffering and backpressure | The public FIFO tutorial inserts a FIFO between host-to-device input and an operation, using separate microthreads to push and pop. It discusses finite capacity and scratch-buffer cost. [Pipeline 2, v2.10.0](https://github.com/Cerebras/sdk-examples/blob/v2.10.0/tutorials/pipeline-02-fifo/README.rst) | Visible messages plus reservations consume bounded capacity. This project defines that accounting itself; it does not reproduce hardware queues, microthreads, or FIFO allocation. |
| Host/device responsibilities | In the GEMV tutorial, the host copies inputs, launches computation, and copies back the final result. [Routes and Fabric DSDs](https://sdk.cerebras.ai/csl/tutorials/gemv-06-routes-1) | C++ workload builders prepare local input; engines execute; a serial oracle checks output. Everything runs within one host process, without SDK launches or device transfers. |

The FIFO tutorial describes an application dataflow pipeline. MeshFlow does not implement CPU pipeline stages or claim pipeline-accurate WSE behavior. Model ticks have no conversion to Cerebras hardware latency, bandwidth, or throughput.

## Simulator references

gem5's [event-driven programming tutorial](https://www.gem5.org/documentation/learning_gem5/part2/events/) explains scheduling event callbacks at future ticks. It provides general background for our priority queue; our phase ordering, PE tie-breaking, retry rule, and inclusive limit are project-defined. No gem5 code or runtime is used.

gem5's [SimpleCPU documentation](https://www.gem5.org/documentation/general_docs/cpu_models/SimpleCPU) distinguishes atomic memory accesses with latency estimates from timing accesses that wait for a response. Our inference is that model detail and host execution cost need separate descriptions. MeshFlow's tick and event executors implement the **same timed contract**; they are not counterparts to those two gem5 CPU models.

These sources establish terminology and context only. This repository contains no private SDK, vendor binaries, imported simulator traces, or reused MeshCompact measurements. Verified project behavior and personal understanding are recorded separately in [resume evidence](resume-evidence.md).
