"""ROM-free tests of GUI coverage declarations and suite result reporting."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "tools/gui_regression"
sys.path.insert(0, str(FOLDER))
try:
    spec = importlib.util.spec_from_file_location("gui_suite", FOLDER / "run_suite.py")
    suite = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(suite)
finally:
    sys.path.pop(0)


class GuiRegressionSuiteTests(unittest.TestCase):
    def test_smb_ju_matrix_covers_each_requested_tab_with_a_pinned_output(self):
        manifest, matrix = suite.load_matrix(FOLDER / "smb1_ju_suite.json")
        self.assertEqual(
            {"levels", "world", "graphics", "metatiles", "palettes", "animations", "screens", "text", "sound"},
            set(manifest["required_kinds"]),
        )
        self.assertEqual({"demo", "playtest"}, set(manifest["excluded_kinds"]))
        self.assertEqual(10, len(matrix))
        for _, scenario in matrix:
            self.assertRegex(scenario["expected_sha256"], r"^[0-9a-f]{64}$")
            self.assertTrue(scenario["baseline"]["rom_differences"])

    def test_removing_the_only_scenario_for_a_tab_cannot_claim_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for path in FOLDER.glob("smb1_ju*.json"):
                shutil.copyfile(path, folder / path.name)
            path = folder / "smb1_ju_suite.json"
            manifest = json.loads(path.read_text())
            manifest["scenarios"].remove("smb1_ju_music_note.json")
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "missing a required tab"):
                suite.load_matrix(path)

    def test_cannot_exclude_editable_tab_to_claim_full_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for source in FOLDER.glob("smb1_ju*.json"):
                shutil.copyfile(source, folder / source.name)
            path = folder / "smb1_ju_suite.json"
            manifest = json.loads(path.read_text())
            manifest["required_kinds"].remove("world")
            manifest["excluded_kinds"].append("world")
            manifest["scenarios"].remove("smb1_ju_world_physics.json")
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "Demo and Playtest"):
                suite.load_matrix(path)

    def test_pending_missing_or_failed_runs_never_become_pass(self):
        for status in ("FAIL", "MISSING_INPUT", "BASELINE_PENDING"):
            with self.subTest(status=status):
                self.assertEqual(status, suite.aggregate_status([{"status": "PASS"}, {"status": status}]))
        self.assertEqual("FAIL", suite.aggregate_status([]))
        rows = [dict(status="BASELINE_PENDING", scenario_id="one", actual_sha256="a" * 64)]
        self.assertTrue(suite.repeat_hashes_match(rows))
        rows.append(dict(status="BASELINE_PENDING", scenario_id="one", actual_sha256="b" * 64))
        self.assertFalse(suite.repeat_hashes_match(rows))

    def run_fake_suite(self, root, child_exit=0, stop_on_fail=False):
        def child(command, **kwargs):
            folder = Path(command[command.index("--out") + 1]) / "independent-run"
            folder.mkdir()
            scenario = suite.run.load_scenario(Path(command[command.index("--scenario") + 1]))
            (folder / "report.json").write_text(
                json.dumps(
                    dict(
                        status="PASS",
                        actual_sha256=scenario["expected_sha256"],
                        stop_reason="synthetic functional fixture",
                    )
                )
            )
            return SimpleNamespace(returncode=child_exit)

        with mock.patch.object(suite.subprocess, "run", side_effect=child) as process:
            with contextlib.redirect_stdout(io.StringIO()):
                code = suite.main(
                    [
                        "--manifest",
                        str(FOLDER / "smb1_ju_suite.json"),
                        "--roms",
                        str(root / "synthetic-input"),
                        "--out",
                        str(root / "out"),
                        "--studio",
                        str(root / "studio"),
                        "--kit",
                        str(root / "kit"),
                        "--case",
                        "smb1_ju_block",
                        "--repeat",
                        "2",
                    ]
                    + (["--stop-on-fail"] if stop_on_fail else [])
                )
        report = json.loads(next((root / "out").glob("*/suite.json")).read_text())
        return code, report, process

    def test_selected_repeats_use_independent_directories_and_report_partial_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code, report, process = self.run_fake_suite(root)
            self.assertEqual(0, code)
            self.assertTrue(report["complete"])
            self.assertFalse(report["full_coverage"])
            self.assertEqual(2, len(report["runs"]))
            directories = []
            for call in process.call_args_list:
                command = call.args[0]
                self.assertEqual(str(root / "kit"), command[command.index("--kit") + 1])
                directories.append(command[command.index("--out") + 1])
            self.assertNotEqual(*directories)

    def test_stop_on_failure_preserves_failed_report_and_incomplete_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            code, report, process = self.run_fake_suite(Path(temporary), child_exit=1, stop_on_fail=True)
            self.assertEqual(1, code)
            self.assertEqual("FAIL", report["status"])
            self.assertFalse(report["complete"])
            self.assertFalse(report["full_coverage"])
            self.assertTrue(report["stopped_early"])
            self.assertEqual(1, process.call_count)
            self.assertEqual(1, len(report["runs"]))

    def test_child_exit_and_report_disagreement_is_a_suite_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            code, report, _ = self.run_fake_suite(Path(temporary), child_exit=1)
            self.assertEqual(1, code)
            self.assertEqual("FAIL", report["status"])
            self.assertTrue(all(row["status"] == "FAIL" for row in report["runs"]))


if __name__ == "__main__":
    unittest.main()
