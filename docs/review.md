# Release review

Review date: 2026-09-09 UTC / 2026-09-08 America/Toronto. This is a source and behavior review of the snapshot identified below, not a claim of exhaustive proof, hardware validation, or personal mastery.

The reviewer is a separate development agent from the EventEngine, CLI, and main golden-test authors. The same reviewer authored `src/tick_engine.cpp` and `tests/fuzz.cpp`; this reviewer's assessment of those two files is therefore **not author-independent**. The golden-test author additionally performed a read-only, non-author source review of TickEngine against SPEC and its helpers and reported no actionable findings, binding the same TickEngine SHA-256 shown below. That source review covered completion/issue ordering, fault partial state, capacity, retry, and limit arithmetic; it involved no additional test runs. No repository files outside this report were modified during the release-review pass.

## Current disposition

No open actionable correctness finding remains in the inspected source and tooling snapshot. The CLI, sanitizer gate, documentation commands, and report-provenance issues below were corrected and rechecked. The primary task then supplied formal CPU results; this reviewer independently checked their source/binary bindings, pair consistency, ordering, and reported ratios without rerunning measurements. Final validation/evidence narratives match the checked records and preserve the model/measurement/authorship boundaries. The source and measured artifacts are suitable to proceed to final delivery verification; final STATE and remote repository configuration remain primary-task delivery checks.

## Findings and follow-up

| ID | Finding and reproduced behavior | Disposition |
| --- | --- | --- |
| R1 | Warmup model failures were thrown into the generic exception handler and returned exit 2, although the CLI contract assigns model failures exit 1. `--workload deadlock --pes 2 --size 0 --warmup 1` and `--max-ticks 0 --warmup 1` both returned exit 2, with no parameterized replay. | **Resolved.** A unified warmup/measured path now returns exit 1 with `phase="warmup"` JSON and replay for both probes. Successful warmup 2 / repeat 2 still returns exactly two verified measured records numbered 0 and 1. Committed CLI test source includes both regressions. |
| R2 | `--trace ''` returned exit 0 and silently disabled tracing because an empty path was indistinguishable from an absent flag. An explicitly requested empty output path should be rejected. | **Resolved.** Explicit empty paths now return exit 2 with no success record. Source and rebuilt Release behavior were checked; CLI regression is present. |
| R3 | The initial `make sanitize` configuration enabled UBSan without fatal recovery policy. A separate compiler-policy probe diagnosed signed overflow but exited 0 under the same default flags; with `UBSAN_OPTIONS=halt_on_error=1`, it exited nonzero. Thus a future UBSan finding could pass an exit-code-only gate. No UB was observed in project runs. | **Resolved.** Make adds `-fno-sanitize-recover=all`, runs both the normal tests and supplementary fuzz, and tracks compiler/options in a build configuration prerequisite. The separate policy probe rebuilt with the final fatal flags emitted the overflow diagnostic and exited nonzero. |
| R4 | Source inspection found that README/CI invoked sweep with a positional binary, while its parser requires `--binary`; README also omitted report's required `--input`. | **Resolved.** README and workflow now use the declared flags. All three tool `--help` invocations passed. No cloud workflow was launched. |
| R5 | Initial report code could accept absent provenance metadata while its generated footer claimed that source/binary provenance was retained. | **Resolved.** Report now requires adjacent passed metadata and source/binary provenance. Final targeted checks reject missing metadata, missing provenance, and nonobject metadata. This validates a structural guard, not the authenticity of arbitrary externally supplied data. |

`build-test-agent/meshflow_tests` was initially an untracked generated binary outside `/build/`. Its author subsequently reported removing the temporary build directory. This was a release-staging item, not a simulator defect; the reviewer did not remove another agent's files. Final staged-file inspection is still part of the primary task's delivery gate.

## What was reviewed

- Normative instruction, timing, FIFO reservation, and termination contracts against both independently implemented execution paths. Particular attention went to completion ordering, first-fault partial state, positive latency arithmetic, inclusive tick limits, and the extra wake after a later PE frees capacity.
- Pure validation/arithmetic helpers, full-state comparator fields, checksum limitations, workload construction, serial oracle independence, and bounded integer arithmetic.
- Hand-calculated tests for state/trace timing, bidirectional communication, full/empty FIFO behavior, overflow, first faults, unconsumed messages, model limits, configuration rejection, input immutability, and repeated-run reset. The tests also cover comparator mutations and check link-history invariants.
- CLI parsing, model failure handling, trace-path errors, integer limits, JSON emission boundaries, timing/RSS scope, and documented exit meanings.
- Makefile and manual-only CI definition. A workflow definition is not evidence of a remote CI execution or of the repository's actual Actions settings.
- Frozen Python sweep/benchmark/report tools. The sweep uses strict typed JSON/result checks; the benchmark prevalidates paired full model state, records declared versus adjacent build-config provenance, alternates engine order, uses per-process warmups, retains unsuccessful partial pairs, and requires the external atomic lock. Report rejects unverified/unpaired/inconsistent rows and produces statistics from raw values. These are source and bounded harness checks, not a performance result.
- Existing README, SPEC, design/debug documentation, Chinese walkthrough, and public-architecture correspondence for scope and evidence boundaries. They distinguish custom model time from hardware time and project delivery from unassessed understanding. Public web references were not independently reopened by this reviewer; their live-source verification belongs to the authoring task.

The initially absent tool scripts, `tests/cli_test.py`, and `tests/README.md` arrived during the pass and were reviewed. Formal raw measurements and the validation manifest subsequently arrived and were verified as detailed below. The final `docs/validation.md` and `docs/resume-evidence.md` were read against these records; counts, source commit, examples, six performance ratios, timing/RSS boundaries, explicit Linux/cloud-CI skips, and unassessed personal understanding are consistent. Every relative Markdown target in these two files exists. Final `STATE.md` and remote repository settings were not independently inspected; statements that the repository is private and Actions is disabled require the primary task's API/readback evidence.

## Checks actually executed by this reviewer

Host: macOS 15.5, arm64. Compiler: Apple clang 17.0.0 (`clang-1700.0.13.5`). These are CPU-hosted checks; no vendor SDK, simulator, or device was invoked.

1. Strict standalone TickEngine object compilation passed with:

   ```sh
   clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude -c src/tick_engine.cpp -o /tmp/meshflow-tick-engine.o
   ```

2. The persisted supplementary fuzzer was freshly built and run under ASan/UBSan:

   ```sh
   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iinclude src/tick_engine.cpp src/event_engine.cpp src/model.cpp src/workloads.cpp tests/fuzz.cpp -o /tmp/meshflow-persisted-fuzz
   /tmp/meshflow-persisted-fuzz
   ```

   Actual output: `PASS 40000 arbitrary-program differential cases (seed=5277971) and 624 workload oracle cases`. Exit 0; no sanitizer diagnostics were emitted. This run preceded R3's fatal-UBSan policy change; it does not assert that the initial flags would fail on every possible UBSan diagnostic. An earlier exploratory stdin-compiled harness ran the same cases successfully; it is not counted as additional coverage.

3. The existing Release main-test binary was independently executed:

   ```sh
   build/release/meshflow-tests
   ```

   Actual output: `cases=757 passed=757 failed=0 checks=114898`. The primary task built this binary. This reviewer inspected test source but did not rebuild the whole project during the release-review pass.

4. Fourteen targeted Release CLI probes were run through Python `subprocess.run` with a five-second external timeout per process. Eleven met the expected exit behavior: default completion; constructed deadlock with comparison; zero model limit; UINT64_MAX rejection; decimal parse overflow; negative PE count; an EventEngine HALT completing at INT64_MAX; a maximum-latency operation limited at tick zero; trace output to a directory; trace output under a missing parent; and conflicting `--trace` / `--no-trace`. The remaining three reproduced R1 twice and R2 once. The timeout was only a probe guard and was not represented as model deadlock.

5. An isolated sanitizer-policy program performed `volatile int n = INT_MAX; volatile int result = n + 1;` and returned 0. With default `-fsanitize=address,undefined`, the process emitted a signed-overflow diagnostic and exited 0. With `UBSAN_OPTIONS=halt_on_error=1`, the same binary emitted the diagnostic and exited nonzero. This was deliberate UB in a temporary policy probe, not a project failure or project source modification.

6. After remediation, the three previously failing CLI probes passed their intended classifications. An additional successful `--workload scan --pes 3 --size 7 --warmup 2 --repeat 2 --compare-engines` invocation produced exactly two verified measured records with repeat IDs 0 and 1. The deliberate sanitizer-policy program was rebuilt with `-fsanitize=address,undefined -fno-sanitize-recover=all`; it now exited nonzero on the deliberate overflow without requiring an environment setting.

7. All three tools' `--help` commands exited 0. An occupied benchmark lock returned exit 2, preserved the other owner's file, and created no output directory. No benchmark process was launched in this occupied-lock check.

8. Temporary, explicitly synthetic CSV fixtures checked report arithmetic and rejection paths. Two pairs with Tick times 100/300 ns and Event times 100/100 ns produced the expected median ratio 2.000. Unpaired, duplicated, unverified, mismatched-model rows and failed metadata were rejected. After the provenance change, missing metadata, missing provenance, and nonobject metadata were rejected; the synthetic arithmetic result remained 2.000 with structurally valid fixture metadata. These temporary fixtures were removed and are not project performance evidence.

The primary task's separate Release receipt at `benchmarks/raw/validation/macos-release.txt` was also read: it records `make -j2 test fuzz`, core 757/757 with 114,898 checks, fuzz 40,000 + 624, and 13 CLI methods / 54 subprocess invocations. The final `macos-sanitize.txt` receipt records the same case/method counts under `make -j2 sanitize` with fatal sanitizer recovery disabled and no emitted sanitizer findings. These are reviewed primary-task receipts, not additional full suites executed by this reviewer.

Local compilation and test execution were paused when the primary task notified the reviewer of another project's exclusive measurement window. No performance benchmark was run by this reviewer.

## Formal measurement and receipt cross-check

The measured implementation is source commit `ce46b7de73709d56456e5be4e8467487d12e1d56`. This reviewer checked every one of the validation manifest's 15 source hashes against both the working files and `git show` at that commit. All five listed raw validation artifact hashes, all six Release/sanitizer binary hashes, and both adjacent build-config strings matched the files on disk. No new simulator execution was used for this check.

The formal benchmark has 84 measured rows, 42 complete Tick/Event pairs, six successful full-state prevalidations, seven pairs per scenario/workload, and two warmups inside each measured process. Every row's configuration, checksum, final model tick, and committed instruction count matches its prevalidation; every pair matches and follows the documented alternating engine order. All measured rows are verified, completed, trace-disabled, and labeled `phase="measured"`, with no model diagnostic or error. The benchmark's source and binary hashes match the same committed implementation, and its adjacent build config records the strict Release flags.

An independent calculation directly from CSV reproduced every reported median Tick/Event ratio, rounded to three decimals: dense reduction **0.731**, relay **0.815**, scan **0.999**; sparse reduction **22.810**, relay **30.714**, scan **70.704**. The dense scan result is effectively parity in this sample, while the other two dense workloads favor Tick. These are observed timings for this custom simulator on the recorded Apple M2, not vendor/device speedups.

The separate `profile-tick-20260909.json` was read and its provenance matched. It is explicitly an instrumented TickEngine baseline using scheduler work counters and elapsed time, **not statistical CPU sampling**. The benchmark records a nonzero host load average (approximately 4.8 over one minute on eight logical CPUs). Seven short paired trials and this host snapshot support the reported observations, not universal or noise-free performance conclusions.

## Reviewed source identity

SHA-256 values below identify the final reread source snapshot after remediation. The initial failing CLI binary was not hashed before replacement, so R1/R2 are reproduced behavior records rather than immutable pre-fix patch evidence. `src/main.cpp` had already been corrected by the first hash capture; its final source and tested binary identities are listed explicitly. Changes after these identities are not implicitly covered. The repository had no commits at initial inspection. This report has no self-hash to avoid a circular identity.

| File | SHA-256 |
| --- | --- |
| `include/meshflow/model.hpp` | `55e46d40123fd0cc8442e27e1864a5715183b9c9c3f8924ff7d66cc8c18155c0` |
| `src/model.cpp` | `749108b652c2085012fab3d850e60b0975078e48553357d4e657b211919c89fc` |
| `src/tick_engine.cpp` | `993118eed5a1f90a2cfb3dad188b242e08cb40b9a452309aca223f667be49931` |
| `src/event_engine.cpp` | `c92a08f240883798493364eeba218b038bab7893e39d24ce8c4f8f04ef8f3b46` |
| `src/workloads.cpp` | `445a6f45ce4a5e2eb48bc57646f8cb29511061e01a862326d59e080e25e35f25` |
| `src/main.cpp` | `4c0c816f3458cd3ff9076185fb618e5394ee755b4a831b4471660f18a44e9afc` |
| `tests/tests.cpp` | `5ca8f13119d3893533e582b4122eeb57fbfe3bd71bf617afba6879c7775d1dc6` |
| `tests/fuzz.cpp` | `8475c2811e2273b5916afe8cbb7663251b20e0a6b0d29e3fdf9b5a83a0ead364` |
| `Makefile` | `f51fd66fee43ea2e3b2f55a854afc186847b1454b66ebb78d1f49ed5f243b7a5` |
| `.github/workflows/ci.yml` | `e96644860ada67f72890dcd6c1efbe8cae5133c7362d07afb0be96fed4e84272` |
| `.gitignore` | `ccbeca07df62a16fcb2e5ea83874aeae61293344da63cd43f9231626bfe13c79` |
| `README.md` | `3df8a0ab9e12e1ab04152f67e0e1e239e625e3e12ace0a6aabeb99f6a7c8c2e0` |
| `SPEC.md` | `ca9b11771cbd114c1383ba0391ddfbf16b9a3c82d645fed85fdd4c424d3b0e4c` |
| `docs/design.md` | `993a76d878d5f84d56643ffc98ab3bb183bb822299478d44803183a799c05cdb` |
| `docs/debug-case.md` | `bce2c7737d91e87081bdcf62b1344ffb19f2b5464eff52411659b176a69af08e` |
| `docs/walkthrough-zh.md` | `4cf5432c51e78475a369ff833cf6089c3f2846dcc2fea041e9ab85f9c7361934` |
| `docs/cerebras-mapping.md` | `500d32bc90c88307e5304f9a1e2d29a0fa36bd18c6699dbbd0880667c25e0917` |
| `tests/cli_test.py` | `e47b4d042b3c0a204707871427f0d814593580eb33a8de1e104094c623cd6d16` |
| `tests/README.md` | `f95b75e84c01a2476ba55a05c03e67e6966eb307bcdead989c33e99bb402dc3d` |
| `tools/sweep.py` | `8a8d1a626e685e622a3888f613a168a69a42bce67f62e2a286aedc0a49bc709e` |
| `tools/benchmark.py` | `0ac1ae4cb7509113131acc188a7fb54924f5276ad1d4685ca9ec55ba00b8b7e3` |
| `tools/report.py` | `9204eeb99a1503f98b0106d4f5dbaa2920f0ec427882f4d212e722903a496098` |
| `docs/validation.md` | `7c14ac7a5609d814ed1084b112bf99f5deea44b463b1e54dcda92437a9d23077` |
| `docs/resume-evidence.md` | `9a37cea836a1258ae57431442c731d097ba2da875533d63bfa434e340dbebbe3` |

Read receipt and executed Release binary identities at the end of this pass:

| Artifact | SHA-256 |
| --- | --- |
| `benchmarks/raw/validation/macos-release.txt` | `365bfb5bd39407160f241b0d6beb787bf591403c251c39d3e1557f26a43a10d6` |
| `build/release/meshflow` | `e0c4392f56b80f047cd5b7b6a67d4fcc3fd2ddad02f464093ea1667f3b833ee4` |
| `build/release/meshflow-tests` | `c8fedd88b156b77290daead435839eeeb68b55ac86ffab30e2806c44fa53ca1f` |
| `benchmarks/raw/validation/manifest.json` | `5d64ba5abb0d8a2c87e4635f15b424b8521a3f1f921a8249d8bc22ae12dc430a` |
| `benchmarks/raw/cpu-m2-20260909/metadata.json` | `1088c8106fdf6270a16c413935133b7a05e0d1d84cd6027cedb5af2fb8a685e9` |
| `benchmarks/raw/cpu-m2-20260909/raw.csv` | `9c7d9d29dc8e0ef19c46ff5f11ee0c93139376f5190adc51fd1595f8e426f134` |
| `benchmarks/raw/cpu-m2-20260909/report.md` | `694a7b2bf0db17a6e73f7eaea390f405da5475060f4353a0b7b6aa157f705998` |
| `benchmarks/raw/profile-tick-20260909.json` | `6366bef5288a6ef6c5eed4291074386b531cd1130903c436a9781a42cd6b32df` |

## Limits

Random differential agreement can preserve shared-helper or specification errors; the handwritten goldens and algorithmic oracles provide different checks but do not prove all programs. The public library is a bounded instructional integer model, and the CLI imposes explicit workload limits. Very large permitted model-time values can make the intentionally scanning TickEngine impractical; a model limit is not a wall-clock timeout.

This review does not establish Linux execution, remote CI success, GitHub privacy settings, final staged-file hygiene, vendor timing accuracy, or the user's independent implementation/understanding. Those require their own concrete records. Measurement provenance and arithmetic were checked, but this is not a claim of externally certified or universally repeatable host performance. No hardware or personal-mastery claim is inferred from passing CPU correctness tests.
