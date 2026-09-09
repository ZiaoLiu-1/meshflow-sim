# MeshFlow Sim

A compact C++20 simulator for neighboring processing elements with local memory, finite message FIFOs, and explicit operation latencies. Two independently implemented schedulers execute the same machine: a readable tick loop and a priority-queue event engine that skips idle model time.

The project focuses on simulator correctness and explainable performance. It does not implement a Cerebras ISA or predict WSE hardware timing. All results are from this custom CPU-hosted model.

## Build and run

Requires a C++20 compiler, Make, and Python 3.10+ for the test/measurement scripts; no third-party library, SDK, GPU, network, or paid service is required.

```sh
make -j2
make test
build/release/meshflow --workload scan --pes 4 --size 65 --seed 7 --compare-engines
build/release/meshflow --workload relay --pes 3 --size 12 --capacity 1 --trace build/relay.jsonl
build/release/meshflow --workload reduction --pes 8 --size 103 --compare-engines
make sanitize
```

The CLI prints JSONL. Exit 0 means the selected engine completed and passed the serial oracle. With `--compare-engines`, it also requires equal complete machine state, link message histories and, by default, committed trace. Exit 1 is a model or verification failure; exit 2 is invalid input or an I/O failure. `--help` lists bounds and latency controls.

## What is implemented

- PC, eight int32 registers, configurable local memory, and explicit busy/blocked/halted/faulted PE states.
- Seven checked instructions: CONST, LOAD, STORE, ADD, SEND, RECV, HALT. ADD rejects overflow.
- Directed neighbor FIFOs with send-time capacity reservations, ordered sequence IDs, and completion-time delivery.
- Independent tick and event issue/completion/termination code; shared types and small pure validation/arithmetic helpers.
- Generated relay, inclusive scan and chunked reduction programs. Serial oracles consume original inputs after simulation.
- Full-state/trace comparison, hand-worked goldens, deterministic sweeps, malformed-program fuzzing, ASan/UBSan, and raw CPU measurement tooling.

The exact issue order matters: completions apply first, then PEs issue in increasing ID. A sender may wait one more tick if a later receiver frees a slot after that sender's turn. [SPEC.md](SPEC.md) makes this behavior executable and testable.

## Reproduce validation and measurements

```sh
python3 tools/sweep.py --binary build/release/meshflow --output build/sweep.jsonl
make fuzz
# Benchmark requires an atomic shared lock directory; choose the same path for competing runs.
python3 tools/benchmark.py --binary build/release/meshflow --lock-dir /tmp/meshflow-measure.lock --output benchmarks/raw/local-run
python3 tools/report.py --input benchmarks/raw/local-run/raw.csv
```

Benchmarks use Release builds, in-process warmups, alternating engine order, successful oracle checks and matched model outputs. `elapsed_ns` includes engine construction/execution/result allocation; it excludes workload generation, oracle/comparison and JSON output. Peak RSS is the **whole process** high-water mark, including program generation and warmups. Simulated ticks and CPU nanoseconds are separate quantities. Dense-event scheduling can cost more than scanning; there is no required speedup target.

Actual machine, compiler, test outcomes, source identities, raw results and limitations are recorded in [validation](docs/validation.md). GitHub Actions is defined as manual-only and disabled in this private repository to prevent unapproved cloud charges; a workflow file is not evidence of a cloud CI pass.

## Read the code

| Entry | Purpose |
| --- | --- |
| [SPEC.md](SPEC.md) / [design](docs/design.md) | Timing, resources, states and failure semantics |
| [model.hpp](include/meshflow/model.hpp) | Small public machine interface |
| [tick_engine.cpp](src/tick_engine.cpp) / [event_engine.cpp](src/event_engine.cpp) | Independent execution paths |
| [tests](tests/tests.cpp) | Hand-calculated state/trace and differential checks |
| [debug case](docs/debug-case.md) | Reproduce and reason about a constructed deadlock |
| [中文代码导读](docs/walkthrough-zh.md) | Eight key functions and one question for each |
| [Cerebras mapping](docs/cerebras-mapping.md) | Public architecture concepts and project simplifications |
| [STATE.md](STATE.md) / [evidence provenance](docs/resume-evidence.md) | Delivery status, sources and unassessed personal understanding |

Scope deliberately stays small: one host thread, one-dimensional neighbor links, unrolled integer programs, one in-flight instruction per PE. No parser/compiler, out-of-order execution, hardware pipeline, cache coherence or device SDK is included.
