#!/usr/bin/env python3
"""Black-box CLI contract tests. Usage: python3 tests/cli_test.py /path/to/meshflow."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


if len(sys.argv) < 2:
    raise SystemExit("usage: cli_test.py /path/to/meshflow [unittest options]")
EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class CLITest(unittest.TestCase):
    invocations = 0

    def invoke(self, *args, expected=0):
        CLITest.invocations += 1
        result = subprocess.run(
            [EXECUTABLE, *map(str, args)], capture_output=True, text=True, timeout=20
        )
        self.assertEqual(
            result.returncode, expected,
            f"args={args!r}\nstdout={result.stdout}\nstderr={result.stderr}",
        )
        return result

    def records(self, result):
        records = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(records, "expected JSONL result records")
        return records

    def test_help(self):
        result = self.invoke("--help")
        self.assertIn("--compare-engines", result.stdout)
        self.assertIn("max-ticks", result.stdout)
        self.assertIn("Exit:", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_default_run_and_json_schema(self):
        result = self.invoke()
        record, = self.records(result)
        required = {
            "workload", "engine", "pes", "size", "seed", "capacity",
            "compute_latency", "memory_latency", "link_latency", "max_ticks",
            "trace_enabled", "phase", "repeat", "termination", "verified", "compared",
            "simulated_ticks", "instructions", "scheduler_events", "clock_steps",
            "pe_checks", "elapsed_ns", "peak_rss_bytes", "checksum", "diagnostic", "error",
        }
        self.assertTrue(required <= record.keys())
        self.assertTrue(record["verified"])
        self.assertEqual(record["termination"], "completed")
        self.assertEqual(record["phase"], "measured")
        self.assertFalse(record["trace_enabled"])
        self.assertFalse(record["compared"])
        for key in ("simulated_ticks", "instructions", "elapsed_ns", "peak_rss_bytes"):
            self.assertIsInstance(record[key], int)
            self.assertGreaterEqual(record[key], 0)
        self.assertEqual(record["error"], "")
        self.assertEqual(result.stderr, "")

    def test_every_workload_with_both_selected_engines(self):
        for workload in ("relay", "scan", "reduction"):
            for engine in ("tick", "event"):
                with self.subTest(workload=workload, engine=engine):
                    result = self.invoke(
                        "--workload", workload, "--engine", engine, "--pes", 3,
                        "--size", 7, "--seed", 19, "--capacity", 2,
                        "--compute-latency", 2, "--memory-latency", 5,
                        "--link-latency", 11, "--compare-engines",
                    )
                    record, = self.records(result)
                    self.assertTrue(record["verified"] and record["compared"])
                    self.assertTrue(record["trace_enabled"])
                    self.assertEqual(record["engine"], engine)
                    self.assertEqual(record["workload"], workload)
                    self.assertEqual(record["seed"], 19)
                    self.assertEqual(record["size"], 7)

    def test_single_pe_empty_inputs(self):
        for workload in ("relay", "scan", "reduction"):
            with self.subTest(workload=workload):
                record, = self.records(self.invoke(
                    "--workload", workload, "--pes", 1, "--size", 0, "--compare-engines"
                ))
                self.assertTrue(record["verified"])
                self.assertGreater(record["instructions"], 0)

    def test_repeat_excludes_warmup_and_resets_run(self):
        records = self.records(self.invoke(
            "--workload", "scan", "--size", 11, "--pes", 3,
            "--warmup", 2, "--repeat", 3, "--compare-engines",
        ))
        self.assertEqual(len(records), 3)
        self.assertEqual([r["repeat"] for r in records], [0, 1, 2])
        for key in ("checksum", "instructions", "simulated_ticks", "scheduler_events"):
            self.assertEqual(len({r[key] for r in records}), 1, key)
        self.assertTrue(all(r["verified"] for r in records))
        self.assertTrue(all(r["phase"] == "measured" for r in records))

    def test_trace_jsonl_matches_committed_count_and_is_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace with "quoted" name.jsonl'
            path.write_text("obsolete payload\n", encoding="utf-8")
            records = self.records(self.invoke(
                "--workload", "relay", "--pes", 3, "--size", 4,
                "--trace", path, "--warmup", 1, "--repeat", 2, "--compare-engines",
            ))
            entries = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(entries), records[-1]["instructions"])
            self.assertEqual(entries[-1]["tick"], records[-1]["simulated_ticks"])
            self.assertEqual(entries[-1]["op"], "HALT")
            keys = {(e["tick"], e["pe"]) for e in entries}
            self.assertEqual(len(keys), len(entries))
            self.assertEqual([(e["tick"], e["pe"]) for e in entries], sorted(keys))
            for entry in entries:
                self.assertEqual(set(entry), {"tick", "pe", "pc", "op", "value", "link", "sequence"})
            send_entries = [e for e in entries if e["op"] == "SEND"]
            self.assertEqual(len(send_entries), 8)
            for link_id in (0, 2):
                self.assertEqual([e["sequence"] for e in send_entries if e["link"] == link_id], [0, 1, 2, 3])

    def test_no_trace_comparison_preserves_result(self):
        args = ("--size", 9, "--pes", 3, "--compare-engines")
        enabled, = self.records(self.invoke(*args))
        disabled, = self.records(self.invoke(*args, "--no-trace"))
        self.assertFalse(disabled["trace_enabled"])
        self.assertTrue(disabled["verified"] and disabled["compared"])
        for key in ("checksum", "instructions", "simulated_ticks"):
            self.assertEqual(enabled[key], disabled[key], key)

    def test_deadlock_is_not_successful_engine_comparison(self):
        result = self.invoke("--workload", "deadlock", "--pes", 2, "--compare-engines", expected=1)
        record, = self.records(result)
        self.assertEqual(record["termination"], "deadlock")
        self.assertEqual(record["simulated_ticks"], 0)
        self.assertFalse(record["verified"])
        self.assertIn("replay:", result.stderr)
        self.assertIn("--workload deadlock", result.stderr)
        self.assertIn("--compare-engines", result.stderr)

    def test_model_limit_is_distinct_from_deadlock(self):
        for engine in ("tick", "event"):
            with self.subTest(engine=engine):
                result = self.invoke(
                    "--engine", engine, "--max-ticks", 0, "--compare-engines", expected=1
                )
                record, = self.records(result)
                self.assertEqual(record["termination"], "model_limit")
                self.assertEqual(record["simulated_ticks"], 0)
                self.assertEqual(record["instructions"], 0)
                self.assertFalse(record["verified"])
                self.assertIn("replay:", result.stderr)

    def test_model_failure_during_warmup_keeps_model_exit_code(self):
        for args in (("--max-ticks", 0), ("--workload", "deadlock", "--pes", 2)):
            with self.subTest(args=args):
                result = self.invoke(*args, "--warmup", 1, expected=1)
                record, = self.records(result)
                self.assertEqual(record["phase"], "warmup")
                self.assertFalse(record["verified"])
                self.assertIn("replay:", result.stderr)

    def test_model_limit_boundary_is_inclusive(self):
        for engine in ("tick", "event"):
            with self.subTest(engine=engine):
                record, = self.records(self.invoke(
                    "--engine", engine, "--workload", "relay", "--pes", 1,
                    "--size", 0, "--max-ticks", 1, "--compare-engines",
                ))
                self.assertEqual(record["termination"], "completed")
                self.assertEqual(record["simulated_ticks"], 1)

    def test_invalid_arguments_are_usage_failures(self):
        invalid = [
            ("--pes", 0), ("--pes", 257), ("--size", 100001),
            ("--capacity", 0), ("--capacity", 1000001),
            ("--seed", 4294967296), ("--seed", -1),
            ("--compute-latency", 0), ("--memory-latency", 0), ("--link-latency", 0),
            ("--compute-latency", 2**63), ("--memory-latency", 2**63),
            ("--link-latency", 2**63), ("--max-ticks", 2**63),
            ("--max-ticks", -1), ("--repeat", 0), ("--repeat", 10001), ("--warmup", 10001),
            ("--size", "1x"), ("--size", "1.0"), ("--size", "+1"),
            ("--size", ""), ("--size", "18446744073709551616"),
            ("--engine", "bogus"), ("--workload", "bogus"),
            ("--workload", "deadlock", "--pes", 1),
            ("--workload", "relay", "--pes", 256, "--size", 100000),
            ("--unknown", 1), ("--pes",), ("--trace", "x", "--no-trace"), ("--trace", ""),
        ]
        for args in invalid:
            with self.subTest(args=args):
                result = self.invoke(*args, expected=2)
                self.assertTrue(result.stderr.strip())
                self.assertEqual(result.stdout, "")

    def test_trace_io_failure_is_nonzero(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing" / "trace.jsonl"
            result = self.invoke("--trace", path, "--size", 0, expected=2)
            self.assertIn("trace", result.stderr)
            self.assertFalse(path.exists())

    def test_stdout_io_failure_is_nonzero(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "readonly"
            path.write_text("")
            with path.open("r") as stream:
                for args in ([], ["--help"]):
                    with self.subTest(args=args):
                        CLITest.invocations += 1
                        result = subprocess.run(
                            [EXECUTABLE, *args], stdout=stream, stderr=subprocess.PIPE,
                            text=True, timeout=20,
                        )
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertIn("cannot write", result.stderr)

    @classmethod
    def tearDownClass(cls):
        print(f"CLI subprocess invocations={cls.invocations}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
