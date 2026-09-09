"""Reject incomplete or mismatched measurements before computing a report."""
import copy
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from report import report  # noqa: E402


class ReportTest(unittest.TestCase):
    def setUp(self):
        original = ROOT / "benchmarks/raw/cpu-m2-20260909"
        with (original / "raw.csv").open(newline="") as stream:
            reader = csv.DictReader(stream)
            self.fields = reader.fieldnames
            self.rows = list(reader)
        self.metadata = json.loads((original / "metadata.json").read_text())
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "raw.csv"

    def write(self):
        with self.path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=self.fields)
            writer.writeheader()
            writer.writerows(self.rows)
        (self.path.parent / "metadata.json").write_text(json.dumps(self.metadata))

    def rejected(self, message):
        self.write()
        with self.assertRaisesRegex(ValueError, message):
            report(self.path)

    def test_original_report_is_unchanged(self):
        self.write()
        expected = (ROOT / "benchmarks/raw/cpu-m2-20260909/report.md").read_text()
        self.assertEqual(report(self.path), expected)

    def test_changed_model_result_is_rejected(self):
        for row in self.rows:
            row["checksum"] = "1"
        self.rejected("model result differs")

    def test_changed_workload_is_rejected(self):
        for row in self.rows:
            row["size"] = "98765"
        self.rejected("workload does not match")

    def test_missing_workload_is_rejected(self):
        self.rows = self.rows[:2]
        self.rejected("missing prevalidated workloads")

    def test_missing_trial_is_rejected(self):
        self.rows = [row for row in self.rows if row["trial"] != "6"]
        self.rejected("trial IDs")

    def test_unpaired_trial_is_rejected(self):
        self.rows.pop()
        self.rejected("unpaired measurement")

    def test_wrong_engine_order_is_rejected(self):
        self.rows[0]["position"] = "1"
        self.rejected("alternating engine order")

    def test_duplicate_measurement_is_rejected(self):
        self.rows.append(copy.deepcopy(self.rows[0]))
        self.rejected("duplicate engine/trial")

    def test_unsuccessful_prevalidation_is_rejected(self):
        self.metadata["prevalidations"][0]["result"]["verified"] = False
        self.rejected("unsuccessful prevalidation")

    def test_duplicate_prevalidation_is_rejected(self):
        self.metadata["prevalidations"].append(copy.deepcopy(self.metadata["prevalidations"][0]))
        self.rejected("duplicate prevalidated workload")

    def test_truncated_row_has_clean_cli_error(self):
        self.write()
        with self.path.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(self.fields)
            writer.writerow([self.rows[0][key] for key in self.fields[:self.fields.index("checksum")]])
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/report.py"), "--input", str(self.path)],
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("missing or extra cells", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
