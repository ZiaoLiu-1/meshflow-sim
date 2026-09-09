# MeshFlow model v1

This is a custom, deterministic, single-host-thread integer machine. It is not a vendor ISA, a WSE timing model, or a CPU pipeline implementation. All ticks below are model time.

## State and instructions

PE IDs are contiguous from zero in a nonempty one-dimensional array. Each PE starts at PC 0 with eight zero int32 registers and its supplied local memory. Programs are explicit C++ instruction vectors; falling off the end faults (no implicit HALT). Memory addresses are immediate element indices. Registers and addresses, including negative indices, are checked when issuing. Unused operands are ignored. Invalid opcodes fault. ADD widens to int64 and rejects int32 overflow before modifying state.

| Instruction | Operands | Completion effect | Latency |
| --- | --- | --- | --- |
| CONST | a=dst, value=literal | register write | compute |
| LOAD | a=dst, b=address | register receives captured local memory | memory |
| STORE | a=src, b=address | local memory receives captured register | memory |
| ADD | a=dst, b=lhs, c=rhs | checked sum to register | compute |
| SEND | a=src, b=neighbor | reserved message becomes visible | link |
| RECV | a=dst, b=neighbor | captured message to register | compute |
| HALT | none | PE becomes halted | compute |

All three configured latencies and FIFO capacity must be positive. `max_ticks` may be zero. To make tick arithmetic unambiguous, latencies and max_ticks must each be <= 2^63-1. Thus adding any issue time <= max_ticks and any latency cannot overflow uint64. Configuration errors throw `std::invalid_argument`; program instruction faults return `fault`.

## Links and issue reservations

Each adjacent pair has two independent directed links. Link `2*min(src,dst)+(src>dst)` identifies one direction. SEND/RECV explicitly name an adjacent PE, with RECV naming the source. Link occupancy is `visible FIFO size + in_flight SEND reservations`, at most capacity. Sequence IDs start at 0 independently on every directed link and are assigned only on successful SEND issue. A SEND captures its source value and reserves space immediately. Its completion appends that message to the visible FIFO and releases the reservation. Sent, delivered and received histories preserve sequence and value.

A RECV removes the oldest visible message at issue, freeing the slot immediately, and writes its destination only on completion. It cannot receive an in-flight message. An empty RECV or full SEND blocks without advancing PC, reserving resources, or adding trace entries. Each PE has at most one issued instruction; no next issue before completion. Input programs and initial memory are immutable across runs.

## Time and ordering

Tick 0 begins with issue. At every later visited tick:

1. Complete all previously issued operations due now, in increasing PE ID. Apply their writes and message deliveries; increment PC and committed instruction count. HALT also increments PC. Append completion trace `(tick, pe, old_pc, op, value, link, sequence)`. Noncommunication entries have link=-1 and sequence=0; HALT value=0. STORE records the stored value. No issue occurs until all completions finish.
2. Visit nonbusy/nonhalted PEs in increasing ID, retry blocked instructions and issue at most one operation per PE. Resource changes in this pass are immediately observable by later IDs. Consequently, a lower-ID blocked sender can miss a slot that a higher-ID receiver frees later in the pass, and retries next tick.
3. Diagnose termination as below, then advance time. An instruction issued at t completes no earlier than t+1.

The tick engine scans each integer tick and every PE. The event engine has a priority queue ordered by `(tick, phase, stable_sequence)`: completion phase 0 uses PE ID as its stable tie key (one pending instruction per PE); a coalesced issue-retry wake is phase 1. It visits each active time only, applies all completions before the issue pass, and schedules t+1 when an earlier blocked PE became eligible later in the issue pass. The next completion otherwise advances time. Idle retry scans never produce committed trace entries.

Faulting instructions have no committed trace entry or PC advance. The first fault encountered during the ordered issue pass sets that PE faulted and stops the simulation immediately, retaining previously committed state and already-issued operations on other PEs (busy status and any link reservations). Diagnostics contain PE, PC and error. Histories are architectural diagnostics, not real hardware traces.

## Termination and limits

Priority after issue is fault, then: all halted with no pending operations and empty FIFOs/reservations => `completed`; all halted with unconsumed messages => `orphaned_messages`; no pending operations and no instruction can make progress => `deadlock`. The engines retry the next tick if a resource became available later in the issue pass. Pending operations that may change state prevent premature deadlock, including operations on PEs that will HALT before another PE consumes a message.

Both engines process completion and issue at max_ticks inclusively. If progress requires a later tick, return `model_limit` with reported tick=max_ticks and the state at that boundary (including operations issued there). The event engine may skip idle ticks to this boundary. Model limit is not deadlock. External process timeout is separate and never a simulated termination reason.

## Equivalence and observability

Compare termination reason, final tick, diagnostic, every PE's PC/status/registers/all allocated memory, each link's visible FIFO/reservations/next sequence/sent-delivered-received histories, committed instruction count, and every committed trace entry when enabled. Engine work statistics (queue pops, clock steps, PE checks) intentionally differ. The comparator reports the first differing field. `--compare-engines` succeeds only if both complete, states match and an independent serial workload oracle passes. Tests may compare expected faults, deadlocks and limits without calling those successful workload runs.

| Work statistic | TickEngine | EventEngine |
| --- | --- | --- |
| instructions | Committed operations, including HALT | Same architectural count |
| scheduler_events | Completed pending operations | Heap records popped, including tick-0 and retry wakes |
| clock_steps | Every visited integer tick, including tick 0 | Active time batches, including tick 0 and a limit-boundary visit |
| pe_checks | PE visits in completion and issue scans | PE visits in issue and post-pass eligibility scans |

PE checks exclude initialization and other bookkeeping; an early issue fault shortens the pass. They are executor-specific instrumentation, not interchangeable CPU instruction counters. All counters reset each run. A captured RECV and an in-flight SEND need not have committed yet, so received/sent history sizes can exceed the corresponding committed instruction counts at a limit or fault.

Relay moves the generated vector through every neighbor to the last PE. Scan distributes contiguous uneven chunks, computes each local prefix from its input, receives the preceding PE's running total, adjusts local output, and sends its total onward. Reduction sums each local chunk, receives and adds the preceding total and passes the result onward. Empty chunks still relay a total. No precomputed output is inserted into the input machine. Values are deterministic integers in [-8,8], generated from a seeded uint32 xorshift stream. P=1 and size=0 are valid. CLI resource bounds are documented in `--help`.
