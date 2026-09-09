# A reproducible receive-only deadlock

Source: a deliberately constructed regression workload in this repository's `make_workload("deadlock", ...)`, created during project development. This is not a vendor bug report, a discovered production failure, or a copied SDK trace.

```sh
build/release/meshflow --workload deadlock --pes 2 --size 0 --compare-engines
```

Expected: exit **1**, `termination="deadlock"`, `verified=false`, simulated tick **0**, zero committed instructions. Both execution results match, but `--compare-engines` correctly refuses to call a failed workload successful. The CLI prints its complete parameterized replay command on stderr.

The generated PE 0 program starts with `RECV r0, neighbor 1`; PE 1 starts with `RECV r0, neighbor 0`. Every visible FIFO is empty. Neither instruction can issue, neither creates a pending completion, and no future event can add a message. This is a model deadlock immediately, not an external timeout.

| tick 0, after issue pass | PE 0 | PE 1 |
| --- | --- | --- |
| PC | 0 | 0 |
| Status | blocked_recv | blocked_recv |
| Register r0 | 0 | 0 |
| Incoming FIFO | empty | empty |
| Pending instruction | none | none |

To repair this *protocol*, one endpoint must initiate a SEND while the other receives. Merely increasing FIFO capacity cannot create a message. A short relay already has that initiating endpoint:

```sh
build/release/meshflow --workload relay --pes 2 --size 1 --seed 7 --compare-engines --trace build/relay-one.jsonl
```

This successful program can spend ticks waiting for a SEND completion. The pending event means it is a legal wait. Conversely, stopping a valid scan at `--max-ticks 0` yields `model_limit`, and sending to a PE that HALTs without receiving eventually yields `orphaned_messages`. Those distinct outcomes are covered by hand-worked tests; neither is silently relabeled as deadlock.

For a mismatch during a sweep, preserve the JSONL row and run its recorded argv. The comparator names the first changed state field; the trace then identifies the committed tick/PE/PC at that boundary. FIFO sent/delivered/received histories are exposed in the C++ result for a debugger or a focused test.
