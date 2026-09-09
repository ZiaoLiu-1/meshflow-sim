# MeshFlow Sim

A small C++20 simulator that runs integer programs on a row of processing elements. Each PE has its own registers and memory, and sends messages to its neighbors through bounded queues.

The same program can run one tick at a time or through an event queue that skips idle time. Keeping both implementations makes it possible to check their results against each other and measure when the extra scheduling machinery pays off. Relay, prefix scan and reduction provide a few programs to try.

## Build and run

You need a C++20 compiler, Make and Python 3.10 or newer. The simulator has no external library or device dependency.

```sh
make -j2
build/release/meshflow --workload scan --pes 4 --size 65 --seed 7 --compare-engines
build/release/meshflow --workload relay --pes 3 --size 12 --capacity 1 --trace build/relay.jsonl
build/release/meshflow --workload reduction --pes 8 --size 103 --compare-engines
make test
```

Each run prints a JSON result with the simulated time, instruction count and verification status. `--compare-engines` checks registers, memory, program counters, message histories and the committed trace. It also checks the workload against a serial reference. `--help` lists the size and latency options.

## How it works

A PE executes one instruction at a time from a seven-instruction set: CONST, LOAD, STORE, ADD, SEND, RECV and HALT. Registers and memory hold int32 values; invalid operands and overflowing addition stop the run with a fault.

Sending reserves a queue slot immediately, but the message becomes visible only when the send completes. A receive waits for a visible message and frees its slot when it starts. At each tick, all completions happen before PEs issue their next instructions in ID order. That order is intentional: a sender can wait an extra tick if a later receiver frees a slot after its turn.

The tick and event engines have separate transition code. They share the model types and small validation helpers. [SPEC.md](SPEC.md) defines the timing rules, and [design.md](docs/design.md) follows a message through the two schedulers.

## Tests and measurements

```sh
make sanitize
make fuzz
python3 tools/sweep.py --binary build/release/meshflow --output build/sweep.jsonl
python3 tools/benchmark.py --binary build/release/meshflow --lock-dir /tmp/meshflow-measure.lock --output benchmarks/raw/local-run
python3 tools/report.py --input benchmarks/raw/local-run/raw.csv
```

Hand-calculated cases check timing and queue behavior; seeded sweeps compare the engines; serial references check the algorithms. See [tests/README.md](tests/README.md) for coverage and a worked two-message example.

The recorded Apple M2 runs show the tradeoff: skipping idle ticks helps with long operation latencies, while heap overhead can make dense workloads slower. [Results and raw data](docs/validation.md) include both outcomes. Elapsed time measures `engine.run`; peak RSS covers the whole process, including program generation and warmups. Use a shared lock path when running competing benchmarks.

## When a run fails

- Exit 1 means a model or verification failure; stderr includes a replay command. The [deadlock example](docs/debug-case.md) is a useful starting point.
- `model_limit` means the run needs more simulated time. Check the program and latency settings before raising `--max-ticks`.
- Exit 2 means invalid arguments or an I/O error. Check `--help`, including the workload size limits.
- A trace path needs an existing parent directory. A new file is written for the final measured run.
- Sweep and benchmark outputs must use a new path. An existing lock belongs to another run and is left intact.

This is a custom model running on one host thread, with a one-dimensional topology and no instruction pipeline. Model ticks do not represent WSE hardware cycles. The [Cerebras correspondence](docs/cerebras-mapping.md) explains the architectural inspiration and simplifications; the [中文代码导读](docs/walkthrough-zh.md) walks through the main functions.
