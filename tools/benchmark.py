#!/usr/bin/env python3
"""Locked, alternating, trace-disabled CPU benchmark with raw provenance."""
import argparse
from contextlib import contextmanager
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys
import uuid

from sweep import ROOT, METRICS, RunFailure, binary_command, command, run_cli

DEFAULT_FLAGS = "-Iinclude -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -O3 -DNDEBUG"


def now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def load():
    return list(os.getloadavg()) if hasattr(os, "getloadavg") else None


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def probe(args):
    try:
        result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=10)
        return result.stdout.strip() if result.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def redact(text):
    # Persist no machine-specific absolute paths from compiler diagnostics.
    return re.sub(r"/(?:[^\s\"'<>]+)", "<absolute-path>", text)


def provenance(binary, compiler, flags):
    paths = [ROOT / "Makefile", ROOT / "SPEC.md"]
    for directory in ("src", "include", "tests", "tools"):
        paths.extend(path for path in (ROOT / directory).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc")
    source_hashes = {path.relative_to(ROOT).as_posix(): digest(path)
                     for path in sorted(paths) if path.is_file()}
    config_path = (ROOT / binary).parent / "config"
    config_text = config_path.read_text().strip() if config_path.is_file() else None
    config_tokens = shlex.split(config_text) if config_text else []
    actual_compiler = config_tokens[0] if config_tokens else compiler
    version = probe([actual_compiler, "--version"])
    cpu = probe(["sysctl", "-n", "machdep.cpu.brand_string"]) if platform.system() == "Darwin" else None
    if not cpu and Path("/proc/cpuinfo").is_file():
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.partition(":")[2].strip()
                break
    status = probe(["git", "status", "--porcelain", "--untracked-files=all", "--",
                    "Makefile", "SPEC.md", "src", "include", "tests", "tools"])
    return dict(platform=dict(system=platform.system(), release=platform.release(),
                              machine=platform.machine(), cpu=cpu or platform.processor() or "unavailable",
                              logical_cpus=os.cpu_count()),
                python_version=platform.python_version(), compiler=Path(actual_compiler).name,
                compiler_version=redact(version.splitlines()[0]) if version else "unavailable",
                declared_compiler=Path(compiler).name, declared_build_flags=flags,
                build_config=(dict(path=config_path.relative_to(ROOT).as_posix(),
                                   command=redact(config_text), sha256=digest(config_path),
                                   source="Makefile-generated adjacent config",
                                   absolute_paths_redacted=redact(config_text) != config_text)
                              if config_text else None),
                build_flags_provenance="adjacent build config when available; declarations alone are not verified build provenance",
                git_commit=probe(["git", "rev-parse", "HEAD"]),
                source_dirty=None if status is None else bool(status),
                source_sha256=source_hashes, binary=binary, binary_sha256=digest(ROOT / binary))


@contextmanager
def measurement_lock(path):
    token = uuid.uuid4().hex
    path.mkdir()  # Atomic; an existing lock is never removed or waited upon.
    owner = path / "owner.json"
    try:
        owner.write_text(json.dumps(dict(owner="meshflow-sim-benchmark", token=token,
                                        pid=os.getpid(), purpose="local CPU measurement",
                                        started_utc=now())) + "\n")
        yield
    finally:
        try:
            if json.loads(owner.read_text()).get("token") == token:
                owner.unlink()
                path.rmdir()
        except (OSError, ValueError):
            # Do not delete another owner's files or contents added by others.
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/release/meshflow")
    parser.add_argument("--output", type=Path, required=True, help="new directory for raw.csv and metadata.json")
    parser.add_argument("--lock-dir", type=Path, required=True, help="shared external atomic mkdir lock")
    parser.add_argument("--trials", type=int, default=7)
    parser.add_argument("--warmup", type=int, default=2, help="warmups inside every measured process")
    parser.add_argument("--size", type=int, default=2048)
    parser.add_argument("--pes", type=int, default=4)
    parser.add_argument("--capacity", type=int, default=1)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--max-ticks", type=int, default=100000000)
    parser.add_argument("--timeout", type=float, default=60, help="per-process wall timeout, not simulated time")
    parser.add_argument("--compiler", default="c++", help="compiler used to build the supplied binary")
    parser.add_argument("--build-flags", default=DEFAULT_FLAGS, help="declare actual build flags; no absolute paths")
    args = parser.parse_args()
    if not (1 <= args.trials <= 10000 and 0 <= args.warmup <= 10000
            and math.isfinite(args.timeout) and args.timeout > 0
            and 1 <= args.pes <= 256 and 0 <= args.size <= 100000
            and args.size * args.pes <= 1000000 and 1 <= args.capacity <= 1000000
            and 0 <= args.seed <= 2**32 - 1 and 0 <= args.max_ticks <= 2**63 - 1):
        parser.error("benchmark parameters exceed CLI bounds")
    if redact(args.build_flags) != args.build_flags:
        parser.error("build flags must not contain absolute paths")
    metadata = None
    try:
        binary = binary_command(args.binary)
        if args.output.exists():
            raise ValueError("output directory already exists; choose a new directory")
        with measurement_lock(args.lock_dir.expanduser().resolve()):
            args.output.mkdir(parents=True)
            metadata_path = args.output / "metadata.json"
            metadata = dict(schema_version=1, status="running", started_utc=now(), load_before=load(),
                            trials=args.trials, warmup_per_process=args.warmup,
                            timeout_seconds=args.timeout, provenance=provenance(binary, args.compiler, args.build_flags),
                            timing_scope="CLI elapsed_ns: one engine.run, excluding program generation and oracle",
                            rss_scope="CLI process peak, including program allocation and warmups; bytes; zero means unavailable",
                            order="tick,event on even trial IDs; event,tick on odd trial IDs",
                            prevalidations=[])
            metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
            fields = ["scenario", "trial", "position", "recorded_utc", "command", "pair_verified",
                      "load_before", "load_after", "workload", "engine", "pes", "size", "seed", "capacity",
                      "compute_latency", "memory_latency", "link_latency", "max_ticks", "trace_enabled",
                      "termination", "verified", "compared", "repeat", "phase", *METRICS, "diagnostic", "error"]
            with (args.output / "raw.csv").open("x", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                try:
                    for scenario, latencies in (("dense", (1, 1, 1)), ("sparse", (32, 128, 256))):
                        for workload in ("relay", "scan", "reduction"):
                            config = dict(workload=workload, pes=args.pes, size=args.size, seed=args.seed,
                                          capacity=args.capacity, compute_latency=latencies[0],
                                          memory_latency=latencies[1], link_latency=latencies[2], max_ticks=args.max_ticks)
                            check = command(binary, config, compare=True)
                            validation = run_cli(check, config, args.timeout, compare=True)
                            metadata["prevalidations"].append(dict(scenario=scenario, command=shlex.join(check), result=validation))
                            for trial in range(args.trials):
                                rows = []
                                try:
                                    for position, engine in enumerate(("tick", "event") if trial % 2 == 0 else ("event", "tick")):
                                        cmd = command(binary, config, engine=engine, warmup=args.warmup)
                                        before = load()
                                        result = run_cli(cmd, config, args.timeout, engine=engine)
                                        rows.append(dict(result, scenario=scenario, trial=trial, position=position,
                                                         recorded_utc=now(), command=shlex.join(cmd),
                                                         load_before=json.dumps(before), load_after=json.dumps(load())))
                                except RunFailure:
                                    for row in rows:
                                        writer.writerow(dict(row, pair_verified=False))
                                    stream.flush()
                                    raise
                                matched = all(rows[0][key] == rows[1][key] == validation[key]
                                              for key in ("checksum", "simulated_ticks", "instructions"))
                                for row in rows:
                                    writer.writerow(dict(row, pair_verified=matched))
                                stream.flush()
                                if not matched:
                                    raise RunFailure("measurement_state_mismatch", dict(scenario=scenario, workload=workload, trial=trial))
                    metadata["status"] = "passed"
                except RunFailure as error:
                    metadata.update(status="failed", failure=dict(kind=error.kind, detail=error.detail))
                except (OSError, ValueError) as error:
                    metadata.update(status="failed", failure=dict(kind="harness_error", error_type=type(error).__name__))
                except KeyboardInterrupt:
                    metadata.update(status="interrupted", failure=dict(kind="operator_interrupt"))
                finally:
                    metadata.update(finished_utc=now(), load_after=load())
                    metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        print(json.dumps(dict(status=metadata["status"], scenarios=6, trials=args.trials)))
        return 0 if metadata["status"] == "passed" else 1
    except (OSError, ValueError) as error:
        print(f"benchmark: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
