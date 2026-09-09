# Executed validation

## CLI and report fixes, September 9

The CLI now returns exit 2 when writing a result or help text fails. The report tool checks that every CSV row matches its prevalidated workload and result, and requires the full set of trials and engine pairs. Missing cells, duplicate measurements and incorrect engine order are rejected before calculating a summary.

`make -j2 test` passed 757 core cases with 114,898 checks, 14 CLI methods with 56 process invocations, and 11 report tests. `make -j2 sanitize` passed the same suites plus 40,000 program differentials and 624 workload cases with ASan/UBSan recovery disabled. The [source hashes and check results](../benchmarks/raw/validation-20260909-cleanup/manifest.json), [Release log](../benchmarks/raw/validation-20260909-cleanup/release.txt) and [sanitizer log](../benchmarks/raw/validation-20260909-cleanup/sanitize.txt) record this revision separately.

These changes do not alter either engine. No new performance measurements were taken. The original CSV still produces exactly the recorded report.

## Original measurement revision

The results below were recorded at source commit **`ce46b7de73709d56456e5be4e8467487d12e1d56`**. The [validation manifest](../benchmarks/raw/validation/manifest.json) identifies that revision's sources, compiler options, binaries and logs. It is a record of that run, rather than a manifest for later source revisions.

Host: Apple M2, arm64, 8 GiB RAM, Darwin 24.5.0 (macOS), Apple Clang 17.0.0 (`clang-1700.0.13.5`), Python 3.14.3. Runs occurred on **2026-09-09 UTC / 2026-09-08 America/Toronto**. No SDK, vendor simulator or device was used.

## Correctness results

| Actual command | Outcome | Raw evidence |
| --- | --- | --- |
| `make -j2 test fuzz` | PASS: 757 core cases / 114,898 checks; 13 CLI methods / 54 process invocations; 40,000 arbitrary-program differentials + 624 workload oracle cases | [Release log](../benchmarks/raw/validation/macos-release.txt) |
| `make -j2 sanitize` | PASS: the same core, CLI and fuzz suites; ASan + UBSan, recovery disabled | [Sanitizer log](../benchmarks/raw/validation/macos-sanitize.txt) |
| `python3 tools/sweep.py --binary build/release/meshflow --output benchmarks/raw/validation/sweep.jsonl` | PASS: 540 CLI configurations, full state and committed trace comparison, serial oracle | [540 replayable rows](../benchmarks/raw/validation/sweep.jsonl) |
| Two documented examples | Expected deadlock exit 1 at tick 0; uneven P=4/N=65 scan exit 0 at tick 437, 413 commits | [Commands/stdout/stderr](../benchmarks/raw/validation/examples.jsonl), [scan trace](../benchmarks/raw/validation/scan65-trace.jsonl) |

Counts describe different harness layers and **must not be added into a count of independent configurations**. The two 540-case matrices differ but overlap in purpose. Fuzz agreement is not an independent functional oracle; 24 hand-calculated golden scenarios supply separate timing/state expectations. [Test taxonomy and a worked FIFO schedule](../tests/README.md) explain that distinction.

Release flags are `-std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -O3 -DNDEBUG`. Sanitizer builds replace the optimization flags with `-O1 -g -fno-omit-frame-pointer` and add `-fsanitize=address,undefined -fno-sanitize-recover=all`. The Makefile records compiler/options in each build directory and invalidates objects when those options change.

## CPU experiment

The [TickEngine baseline profile](../benchmarks/raw/profile-tick-20260909.json) ran the same scan at P=4/N=2048. Changing only latencies increased its clock visits from **7,698 to 738,881**, with **12,311 committed instructions** in both. The event engine avoids most of these idle visits. These counts come from scheduler instrumentation, rather than sampled CPU profiling.

Then run, under the shared local measurement lock:

```sh
python3 tools/benchmark.py --binary build/release/meshflow --lock-dir /tmp/meshflow-measure.lock --output benchmarks/raw/local-run --trials 7 --warmup 2 --size 2048 --pes 4
python3 tools/report.py --input benchmarks/raw/local-run/raw.csv
```

Use the same lock path for competing runs. No compiler or build process was active during the recorded measurement window. The 8-logical-CPU workstation had load averages approximately **4.84 / 4.69 / 4.07** at measurement start and finish; ordinary applications remained active.

Actual run: **84 measured rows / 42 pairs / six full-state prevalidations** passed. Each measured process warms its selected engine twice, records one run, and verifies the serial oracle. Engine order alternates per trial. Every measured pair must match its prevalidation's checksum, simulated ticks and committed instructions; prevalidation already compared the full architecture. Tracing is disabled equally for both engines.

| Latency model; P=4, N=2048, capacity=1, seed=7 | Tick median ms | Event median ms | Tick/Event |
| --- | ---: | ---: | ---: |
| Dense reduction (1/1/1) | 0.069 | 0.094 | 0.731 |
| Dense relay (1/1/1) | 0.534 | 0.655 | 0.815 |
| Dense scan (1/1/1) | 0.418 | 0.418 | 0.999 |
| Sparse reduction (32/128/256) | 2.147 | 0.094 | 22.810 |
| Sparse relay (32/128/256) | 27.296 | 0.889 | 30.714 |
| Sparse scan (32/128/256) | 29.201 | 0.413 | 70.704 |

Latency triples are compute/memory/link **model ticks**. The ratio uses unrounded medians. Event scheduling is slower for dense reduction/relay and effectively tied for dense scan; sparse models benefit from skipping many idle scans. These observations are not a universal speedup, parallel scaling result, or hardware performance prediction. Dense runs are short, and the host was not frequency-pinned or isolated; seven pairs support a reproducible small experiment, not confidence intervals.

[Raw CSV](../benchmarks/raw/cpu-m2-20260909/raw.csv), [metadata with source and binary identities](../benchmarks/raw/cpu-m2-20260909/metadata.json), and the [mechanically derived report](../benchmarks/raw/cpu-m2-20260909/report.md) retain all trials, min/max, work counters and process peak RSS. The metadata confirms `source_dirty=false` at the validated implementation commit. `elapsed_ns` times the complete `engine.run` call including initialization/allocation, but excludes program generation, oracle, comparison and JSON output. RSS includes the entire process and warmups; it is not per-engine memory usage. [SPEC](../SPEC.md) defines the executor-specific scheduler counters.

## Scope not exercised

- Linux GCC/Clang: not run in these records. The local `g++` name resolves to Apple Clang.
- GitHub-hosted CI: not run. The workflow is manual-only and repository Actions is disabled.
- TSan: not run; the simulator uses one host thread.
- Vendor ISA compatibility, WSE timing, real accelerator execution and hardware performance: outside the implemented model.
