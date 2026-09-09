#!/usr/bin/env python3
"""Deterministic CLI differential matrix; also supplies strict CLI helpers."""
import argparse
import itertools
import json
import math
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
METRICS = ("simulated_ticks", "instructions", "scheduler_events", "clock_steps",
           "pe_checks", "elapsed_ns", "peak_rss_bytes", "checksum")


class RunFailure(RuntimeError):
    def __init__(self, kind, detail):
        super().__init__(kind)
        self.kind, self.detail = kind, detail


def binary_command(value):
    path = Path(value).expanduser()
    path = (ROOT / path).resolve() if not path.is_absolute() else path.resolve()
    try:
        relative = path.relative_to(ROOT)
    except ValueError as error:
        raise ValueError("binary must be inside this repository") from error
    if not path.is_file():
        raise ValueError("binary does not exist; build the project first")
    return "./" + relative.as_posix()


def command(binary, config, engine="event", compare=False, trace=False, warmup=0):
    args = [binary]
    for key, value in config.items():
        args.extend(["--" + key.replace("_", "-"), str(value)])
    args.extend(["--engine", engine, "--warmup", str(warmup), "--repeat", "1"])
    if compare:
        args.append("--compare-engines")
    if not trace:
        args.append("--no-trace")
    return args


def run_cli(args, config, timeout, engine="event", compare=False, trace=False):
    def failure(kind, detail):
        return RunFailure(kind, dict(detail, replay=shlex.join(args)))

    try:
        proc = subprocess.run(args, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        raise failure("wall_timeout", {"timeout_seconds": timeout}) from error
    except OSError as error:
        raise failure("launch_error", {"error_type": type(error).__name__}) from error
    try:
        result = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        raise failure("invalid_json", {"returncode": proc.returncode,
                                       "stdout": proc.stdout, "stderr": proc.stderr}) from error
    expected = dict(config, engine=engine, compared=compare, trace_enabled=trace, phase="measured",
                    verified=True, termination="completed", error="", diagnostic="", repeat=0)
    if (proc.returncode != 0 or not isinstance(result, dict)
            or any(type(result.get(key)) is not type(value) or result[key] != value
                   for key, value in expected.items())
            or any(type(result.get(key)) is not int or result[key] < 0 for key in METRICS)):
        raise failure("unverified_result", {"returncode": proc.returncode,
                                           "result": result, "stderr": proc.stderr})
    if proc.stderr:
        raise failure("unexpected_stderr", {"stderr": proc.stderr, "result": result})
    return result


def matrix():
    for workload, pes, capacity, seed, latencies in itertools.product(
            ("relay", "scan", "reduction"), (1, 2, 3, 4, 8), (1, 2, 4), (1, 17),
            ((1, 1, 1), (2, 3, 7))):
        for size in (0, 1, 2 * pes + 1):
            yield dict(workload=workload, pes=pes, size=size, seed=seed, capacity=capacity,
                       compute_latency=latencies[0], memory_latency=latencies[1],
                       link_latency=latencies[2], max_ticks=1000000)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/release/meshflow")
    parser.add_argument("--output", type=Path, required=True, help="new raw JSONL file; never overwritten")
    parser.add_argument("--timeout", type=float, default=30, help="per-process wall seconds")
    parser.add_argument("--limit-cases", type=int, help="smoke-test prefix only; omit for the full 540-case matrix")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.limit_cases is not None and args.limit_cases < 1:
        parser.error("limit-cases must be positive")
    try:
        binary = binary_command(args.binary)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8") as output:
            cases = itertools.islice(matrix(), args.limit_cases) if args.limit_cases is not None else matrix()
            for case, config in enumerate(cases, 1):
                cmd = command(binary, config, compare=True, trace=True)
                row = dict(case=case, config=config, replay=shlex.join(cmd))
                try:
                    row.update(status="passed", result=run_cli(
                        cmd, config, args.timeout, compare=True, trace=True))
                except RunFailure as error:
                    row.update(status=error.kind, failure=error.detail)
                    output.write(json.dumps(row, sort_keys=True) + "\n")
                    output.flush()
                    print(json.dumps(dict(status="failed", case=case, kind=error.kind,
                                          replay=row["replay"])))
                    return 1
                output.write(json.dumps(row, sort_keys=True) + "\n")
                output.flush()
        print(json.dumps(dict(status="passed", cases=case, traces_compared=True, full_matrix=case == 540)))
        return 0
    except (OSError, ValueError) as error:
        print(f"sweep: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
