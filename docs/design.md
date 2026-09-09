# Design and implementation

[SPEC.md](../SPEC.md) is the normative contract. MeshFlow is a single-host-thread C++20 integer machine with a one-dimensional array of PEs. Its configured ticks describe this machine only.

## State ownership

[`Program`, `PEState`, and `LinkState`](../include/meshflow/model.hpp#L18) separate immutable input from the state returned by each run. A PE owns its PC, eight int32 registers, local memory, and status. A run copies initial memory and starts registers at zero; neither executor writes the supplied program. Each adjacent pair owns two independent directed FIFO links.

The usual status transitions are:

```text
running ── successful issue ──> busy ── completion ──> running
running ── full SEND ─────────> blocked_send ── retry succeeds ──> busy
running ── empty RECV ────────> blocked_recv ── retry succeeds ──> busy
busy ── HALT completion ──────> halted
running / blocked ── invalid issue ──> faulted
```

Each PE has at most one pending operation. Operands are checked and captured at issue. The eventual completion uses the captured value, so a SEND reservation already contains its message and a RECV has already selected its message. Register/local-memory writes and PC advancement occur at completion. HALT also commits and advances PC; falling off the instruction vector is a fault.

[`instruction_error` and `checked_add`](../src/model.cpp#L16) are shared pure helpers. Register, address, neighbor, and opcode errors are issue faults; ADD widens before rejecting int32 overflow. [`validate`](../src/model.cpp#L6) rejects invalid configurations before allocating execution state. Positive latencies ensure that an operation cannot complete in its issue tick; the configured upper bounds keep due-time addition within uint64.

## FIFO capacity and visibility

For each link, the invariant is:

```text
visible FIFO size + in-flight SEND reservations <= configured capacity
```

A successful SEND captures a register, assigns the next sequence number on that directed link, increments `in_flight`, and appends to `sent`. Completion moves the same message into the visible FIFO and `delivered`, and releases its reservation. The capacity counts both stages, preventing senders from overbooking space during link latency.

A successful RECV pops the oldest visible message and appends to `received` immediately. This releases capacity during the issue pass; its register write still waits for compute latency. In-flight messages cannot be received. Blocking does not advance PC or allocate a message sequence number. The three histories expose issued, delivered, and consumed messages separately, including partial state at faults and limits.

## Ordered time steps

Both executors complete all operations due at a tick in increasing PE ID, then issue in increasing PE ID. Completion writes and deliveries therefore precede every new issue. A later PE sees resource changes made by earlier PEs within that issue pass.

The reverse direction requires care. Suppose capacity is one, PE 0 has already filled the link, and PE 1 becomes ready to receive at tick 6:

| Tick | Ordered effect |
| --- | --- |
| 6 | PE 0 retries SEND and blocks; PE 1 then issues RECV and frees the slot. |
| 7 | PE 0 retries successfully and reserves the slot. |
| 8 | With link latency one, that SEND completes and becomes visible. |

PE 0 cannot issue a second time at tick 6. The event executor must schedule tick 7 even if its next previously pending completion is later.

[`TickEngine::run`](../src/tick_engine.cpp#L24) stores one optional pending operation per PE and scans every integer tick. [`EventEngine::run`](../src/event_engine.cpp#L32) stores captured completion records in a priority queue ordered by `(tick, phase, stable_sequence)`. Completion phase 0 uses PE ID for ties. A phase-1 wake starts tick zero or coalesces all eligible next-tick retries. Popping a wake does not execute an instruction; after all events for a tick are popped, there is one ordered issue pass. Without such a retry, the event executor jumps to the next completion.

The implementations independently own their issue, completion, reservation, and termination transitions. Sharing types and pure validation/arithmetic helpers keeps interfaces consistent while leaving the scheduling implementations independently reviewable.

## Termination and comparison

An issue fault returns immediately with PE/PC/error, keeping earlier completions and operations already issued by lower IDs. There is no completion trace entry for the faulting instruction. After a fault-free issue pass, both executors apply this priority:

1. All PEs halted and no pending operations: `completed` if every link is empty, otherwise `orphaned_messages`.
2. No pending operations and no currently eligible retry: `deadlock`.
3. Further progress requires a tick beyond `max_ticks`: `model_limit`.

The limit is inclusive. Completions and issue at `max_ticks` are processed; pending operations due later remain represented by Busy states and link reservations. If the event queue jumps past the limit, it visits the boundary once without applying later completions. A process timeout is external to these rules.

[`difference`](../src/model.cpp#L66) compares termination, tick, diagnostics, all PE fields, complete link state and histories, committed instruction count, and enabled completion traces. It returns the first differing field. Scheduler work counters are intentionally excluded. A checksum is only a compact identity and cannot establish equivalence.

[`make_workload` and `oracle_error`](../src/workloads.cpp#L10) keep machine construction separate from serial algorithm checks. Relay forwards input through every hop; scan and reduction distribute contiguous, possibly empty chunks and pass running totals. Generated programs compute results from input memory, and the oracle independently checks returned memory against the input sequence. Exact timing still requires hand-calculated golden cases in addition to differential and workload checks.

## Cost boundaries

Tick scheduling performs work proportional to the visited ticks times PE count. Event scheduling replaces idle ticks with heap operations and active-time PE scans; it still scans PEs at each active time. Large configured latencies can favor skipping, while dense completions can make heap overhead significant. Both runs retain communication histories; trace storage is optional. Workload generation, history storage, and comparison are real costs, so benchmark timing boundaries and memory accounting must remain explicit.

The model has no host parallelism, caches, instruction pipeline stages, task picker, dynamic routing, or vendor binary decoder. Performance results measure the host implementation of this specific contract.
