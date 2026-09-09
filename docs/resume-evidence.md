# Project evidence and attribution

Repository: **https://github.com/ZiaoLiu-1/meshflow-sim** (private). Validated implementation commit: **`ce46b7de73709d56456e5be4e8467487d12e1d56`**. Later result/documentation commits preserve that exact implementation; `git log` and [STATE](../STATE.md) record delivery. This file is a project handoff, not authorization to change the workspace resume evidence ledger.

## Implementation source and authorship

Ziao authorized and selected the bounded project plan. This new implementation was developed with Codex assistance in the MeshFlow task, with separate agents implementing the two execution engines and another constructing hand-calculated golden tests. Additional agents reviewed source and tooling; the final release review explicitly identifies files its reviewer authored. The C++ source and Python harnesses were newly created in this repository. Public Cerebras/gem5 documentation informed terminology and design context, not copied implementation.

No private SDK, vendor binaries, old MeshCompact code/results, course submissions, application materials or remote connection information are repository inputs. Local compile/test and CPU benchmark logs come from this project. The coordinator-provided workspace `AGENTS.md` remains local and is excluded from Git.

**Personal understanding is unassessed.** Independent reproduction, ability to explain/modify the code and oral verification have not occurred. Project completion, a passing test, a Git author field or a private repository URL does not establish those abilities. Every question in [the Chinese walkthrough](walkthrough-zh.md) is pending. No learner mastery or canonical `evidence.json` was changed by this task.

## Verifiable behavior

| Behavior | Source and real verification | Claim boundary |
| --- | --- | --- |
| Custom state/instruction model | [SPEC](../SPEC.md), [model types](../include/meshflow/model.hpp), checked instruction/ADD helpers | PC, eight int32 registers, local memory and seven instructions; no vendor ISA |
| Independent schedulers | [TickEngine](../src/tick_engine.cpp), [EventEngine](../src/event_engine.cpp) | Separate issue, completion, blocking and termination state machines; shared types/pure validation helpers |
| FIFO timing/backpressure | Capacity reservation at SEND issue, visibility at completion, ordered sequence histories; forward/reverse hand goldens | Project-defined one-dimensional adjacent channels; no hardware queue validation |
| Functional and timing correctness | 757 core cases / 114,898 checks, including 24 hand-calculated scenarios; 540 external CLI configurations with full trace comparison | Counts are exact executed harness results, not exhaustive proof or independent-case totals |
| Robustness tooling | 13 CLI methods / 54 invocations; 40,000 arbitrary-program differentials + 624 oracle cases; final ASan/UBSan fail-closed pass | No UB observed in exercised paths; single host thread, no TSan claim |
| Real CPU measurement | 84 measured rows / 42 matched pairs, seven repeats for each engine/workload/model, warmups, alternate order, raw CSV and source/binary binding | Apple M2 host costs for this simulator only; dense event overhead and sparse benefit both retained |

All commands, exact flags, platform, pass/fail/skip states and hashes are in [validation](validation.md) and its [machine-readable manifest](../benchmarks/raw/validation/manifest.json). The [performance metadata](../benchmarks/raw/cpu-m2-20260909/metadata.json) binds the same clean implementation revision and Release binary to the measurements. [Review](review.md) records independently found and verified fixes. No failed exploratory output was repackaged as a final pass.

## Conservative description candidates for coordinator review

These are project descriptions awaiting the coordinator's evidence/admission review, not approved claims of independent authorship or mastery:

- Developed a C++20 processing-element simulator with local memory, bounded FIFO channels, deterministic instruction timing, and independent tick/event schedulers.
- Validated complete architectural state and committed traces with hand-calculated cases, serial relay/scan/reduction oracles, seeded differential tests, and ASan/UBSan.
- Built Python replay and benchmark tooling to compare CPU simulator costs under matched dense/sparse latency models, preserving raw trials and source/binary identities.

If a number is useful, name the exact experiment: **P=4, N=2048, capacity=1, seed=7, Apple M2, compute/memory/link=32/128/256 model ticks**. Sparse scan median Tick/Event was **70.704** for seven paired trials; dense scan was **0.999**, with dense relay/reduction slower under Event. Prefer the qualitative tradeoff if there is insufficient space for that context. Never describe these ratios as accelerator speedup, WSE accuracy, production simulator performance, or hardware scalability.

No Linux compiler pass, cloud CI pass, SDK run, hardware/DV collaboration, real pipeline accuracy or personally mastered C++ competency is established by this evidence. The root coordinator owns any downstream resume updates; no public release or application submission is part of this task.
