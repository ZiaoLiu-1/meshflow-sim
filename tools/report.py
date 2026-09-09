#!/usr/bin/env python3
"""Derive a paired benchmark Markdown report from raw.csv using stdlib only."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
from statistics import median
import sys

GROUP = ("scenario", "workload", "pes", "size", "seed", "capacity", "compute_latency",
         "memory_latency", "link_latency", "max_ticks")
NUMERIC = ("trial", "position", "elapsed_ns", "simulated_ticks", "instructions", "scheduler_events",
           "clock_steps", "pe_checks", "peak_rss_bytes", "checksum")
STATE = ("checksum", "simulated_ticks", "instructions")
REQUIRED = set(GROUP + NUMERIC) | {
    "pair_verified", "verified", "termination", "trace_enabled", "engine",
    "phase", "compared", "repeat", "diagnostic", "error",
}


def expected_runs(metadata):
    count = metadata.get("trials")
    checks = metadata.get("prevalidations")
    if type(count) is not int or not 1 <= count <= 10000:
        raise ValueError("metadata has an invalid trial count")
    if not isinstance(checks, list) or not checks:
        raise ValueError("metadata has no prevalidated workloads")
    expected = {}
    for check in checks:
        if not isinstance(check, dict) or not isinstance(check.get("result"), dict):
            raise ValueError("invalid prevalidation record")
        result = check["result"]
        if (result.get("verified") is not True or result.get("compared") is not True
                or result.get("termination") != "completed"
                or any(type(result.get(key)) is not int or result[key] < 0 for key in STATE)):
            raise ValueError("metadata includes an unsuccessful prevalidation")
        group = tuple(str(check["scenario"] if key == "scenario" else result[key]) for key in GROUP)
        if group in expected:
            raise ValueError("duplicate prevalidated workload")
        expected[group] = tuple(result[key] for key in STATE)
    return expected, set(range(count))


def report(path):
    groups = defaultdict(lambda: defaultdict(dict))
    metadata_path = path.parent / "metadata.json"
    if not metadata_path.is_file():
        raise ValueError("adjacent metadata.json is required")
    metadata = json.loads(metadata_path.read_text())
    if not isinstance(metadata, dict) or metadata.get("status") != "passed":
        raise ValueError("adjacent metadata does not describe a passed benchmark")
    provenance = metadata.get("provenance")
    if not isinstance(provenance, dict) or not provenance.get("source_sha256") or not provenance.get("binary_sha256"):
        raise ValueError("metadata is missing source/binary provenance")
    expected, trial_ids = expected_runs(metadata)
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if None in row or any(row.get(key) is None for key in REQUIRED):
                raise ValueError("CSV row has missing or extra cells")
            if (row["pair_verified"] != "True" or row["verified"] != "True"
                    or row["termination"] != "completed" or row["trace_enabled"] != "False"
                    or row["engine"] not in ("tick", "event") or row["phase"] != "measured"
                    or row["compared"] != "False" or row["repeat"] != "0"
                    or row["diagnostic"] or row["error"]):
                raise ValueError("CSV contains an unverified or trace-enabled measurement")
            for field in NUMERIC:
                row[field] = int(row[field])
                if row[field] < 0:
                    raise ValueError("CSV contains a negative metric")
            group = tuple(row[key] for key in GROUP)
            if group not in expected:
                raise ValueError("CSV workload does not match a prevalidation")
            if tuple(row[key] for key in STATE) != expected[group]:
                raise ValueError("CSV model result differs from its prevalidation")
            first = "tick" if row["trial"] % 2 == 0 else "event"
            if row["position"] != (0 if row["engine"] == first else 1):
                raise ValueError("CSV does not follow the recorded alternating engine order")
            pair = groups[group][row["trial"]]
            if row["engine"] in pair:
                raise ValueError("duplicate engine/trial row")
            pair[row["engine"]] = row
    if set(groups) != set(expected):
        raise ValueError("CSV is missing prevalidated workloads")
    timing, work = [], []
    for group, trials in sorted(groups.items()):
        if set(trials) != trial_ids:
            raise ValueError("CSV trial IDs do not match the metadata trial count")
        for pair in trials.values():
            if set(pair) != {"tick", "event"}:
                raise ValueError("unpaired measurement")
        records = {engine: [pair[engine] for pair in trials.values()] for engine in ("tick", "event")}
        times = {engine: [row["elapsed_ns"] / 1e6 for row in rows] for engine, rows in records.items()}
        if median(times["event"]) <= 0:
            raise ValueError("event median is zero; ratio is undefined")
        label = (f"{group[0]} / {group[1]} (P={group[2]}, N={group[3]}, cap={group[5]}, "
                 f"seed={group[4]}, latency={group[6]}/{group[7]}/{group[8]})")
        formatted = {engine: f"{median(values):.3f} [{min(values):.3f}, {max(values):.3f}]"
                     for engine, values in times.items()}
        timing.append(f"| {label} | {len(trials)} | {formatted['tick']} | {formatted['event']} | "
                      f"{median(times['tick']) / median(times['event']):.3f} |")
        for engine, rows in records.items():
            values = [f"{median([row[key] for row in rows]):g}" for key in
                      ("instructions", "simulated_ticks", "scheduler_events", "clock_steps", "pe_checks")]
            rss = median([row["peak_rss_bytes"] for row in rows]) / 1048576
            work.append(f"| {label} | {engine} | " + " | ".join(values) + f" | {rss:.2f} |")
    return "\n".join([
        "# CPU benchmark report", "", "Derived from the paired rows in `raw.csv`; no inferred hardware measurements.", "",
        "Times are CLI engine-run elapsed milliseconds. Brackets show observed min/max, not confidence intervals.",
        "The ratio is median Tick time / median Event time; values above one favor Event. Trial order alternates.", "",
        "| Model/workload | Pairs | Tick median [min, max] ms | Event median [min, max] ms | Tick/Event |",
        "| --- | ---: | ---: | ---: | ---: |", *timing, "",
        "Scheduler columns are medians. Their definitions differ by executor; simulated ticks describe the model, not host time.",
        "RSS is the process peak including program construction and warmups, in MiB; zero means unavailable.", "",
        "| Model/workload | Engine | Instructions | Final tick | Scheduler events | Clock steps | PE checks | Peak RSS MiB |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |", *work, "",
        "Exact commands, latency parameters, source/binary hashes, declared compiler flags, load samples, and timing boundaries",
        "are retained in `raw.csv` and adjacent `metadata.json`. These results compare this custom simulator on one host CPU.", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="new Markdown file; stdout if omitted")
    args = parser.parse_args()
    try:
        content = report(args.input)
        if args.output:
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(content)
        else:
            print(content, end="", flush=True)
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(f"report: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
