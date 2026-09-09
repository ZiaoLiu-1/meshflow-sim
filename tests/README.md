# Test evidence and independence

`tests.cpp` uses only the C++ standard library. It contains 757 named cases:

- 24 manually calculated instruction, timing and termination cases. Some cases inspect multiple boundaries. Expected PCs, register arrays, all local memory, link FIFO/reservations/histories, committed counts and trace entries are constructed without calling either engine or workload oracle.
- 41 invalid-instruction/configuration and arithmetic-limit cases.
- 26 mutations checking every architectural comparator category and deliberately excluding scheduler work statistics.
- 540 successful relay/scan/reduction configurations: P=1/2/3/4/8, capacity=1/2/4, empty/single/uneven/17-element inputs, three latency triples, deterministic seeds. Both complete and match the serial oracle; full state and trace must match each other.
- Six generator, reset, trace-toggle and corrupted-result oracle cases.
- 120 seeded bounded arbitrary programs, including programs that stop without successful completion. Full engine differential and FIFO/history invariants still apply.

The core suite reports named cases and individual assertion counts separately. A failed case stops at its first failed assertion and does not prevent the remaining cases from running. Therefore the assertion count depends on whether earlier checks pass.

The important forward-link hand schedule uses capacity 1, compute/link latency 1 and memory latency 5. PE0 sends 5 then 8; PE1 loads 99 before receiving twice:

| Tick | Hand-calculated architectural effect |
| --- | --- |
| 1 | PE0 CONST commits; SEND(5, sequence 0) reserves a slot but remains invisible. |
| 2 | SEND(5) becomes visible; PE0 issues CONST(8). |
| 3 | CONST(8) commits; second SEND blocks without assigning a sequence. |
| 5 | PE1 LOAD commits; PE0 still sees a full FIFO during its earlier issue turn. PE1 then removes 5, freeing the slot; its destination register is still zero. |
| 6 | PE1 RECV(5) commits; PE0 retries and reserves SEND(8, sequence 1). PE1's next RECV cannot see it yet. |
| 7 | SEND(8) commits, then PE1 can issue RECV(8). |
| 8 | PE0 HALT and PE1 RECV(8) commit in PE order. |
| 9 | PE1 HALT commits and the machine completes. |

The mirrored program completes at tick 8 because lower-ID PE0 can free the slot before higher-ID PE1's issue turn at tick 5. This one-tick asymmetry is intentional in SPEC.md and has independent goldens in both directions.

`cli_test.py` treats the binary as an external process and uses Python's standard library. It verifies JSONL records, all workloads and both selected engines, warmup/repeat behavior, trace replacement/count/order/sequence, usage and IO errors, inclusive limits, and non-successful comparison of deadlocked models. The script reports unittest methods and subprocess invocations separately.

`fuzz.cpp` is a separate, larger seeded differential probe. Its agreement is additional evidence rather than an independent functional oracle.

`report_test.py` uses temporary copies of the recorded CSV and metadata. It checks that the original report is reproduced exactly, then removes trials, alters results and corrupts rows to check that incomplete or inconsistent measurements are rejected.

Run `make test` for core, CLI and report tests; `make sanitize` runs the C++ binaries under ASan/UBSan and includes the Python checks; `make fuzz` runs the additional probe. Recorded commands, toolchain identity and results belong in `docs/validation.md`; this file describes how the test evidence is constructed.
