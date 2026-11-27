"""ROM-free tests for the private real-widget GUI regression runner."""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gui_regression_run", ROOT / "tools/gui_regression/run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class GuiRegressionRunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.roms = self.root / "roms"
        self.roms.mkdir()
        self.out = self.root / "out"
        self.studio = self.root / "studio"
        self.studio.mkdir()
        self.rom_bytes = b"synthetic runner fixture, not a game ROM"
        self.input_sha256 = hashlib.sha256(self.rom_bytes).hexdigest()
        self.scenario = json.loads((ROOT / "tools/gui_regression/smb1_ju_block.json").read_text(encoding="utf-8"))
        self.scenario["id"] = "runner-test"
        self.scenario["input_sha256"] = self.input_sha256
        self.scenario_path = self.root / "scenario.json"
        mock.patch.object(runner, "provenance", return_value={}).start()
        self.verification = mock.patch.object(
            runner,
            "verify_game_source",
            return_value={"method": "synthetic verified source"},
        ).start()
        self.addCleanup(mock.patch.stopall)

    def write_scenario(self, expected="unchanged"):
        if expected != "unchanged":
            self.scenario["expected_sha256"] = expected
        self.scenario_path.write_text(json.dumps(self.scenario), encoding="utf-8")

    def make_executable(self):
        executable = runner.studio_executable(self.studio)
        executable.write_bytes(b"test double")
        return executable

    def arguments(self):
        return [
            "--roms",
            str(self.roms),
            "--scenario",
            str(self.scenario_path),
            "--out",
            str(self.out),
            "--studio",
            str(self.studio),
            "--kit",
            str(self.root / "kit"),
        ]

    def invoke_with_native_result(self, status, exit_code, expected, source_archive=None):
        actual = "a" * 64
        self.verification.reset_mock()
        (self.roms / "renamed.data").write_bytes(self.rom_bytes)
        self.write_scenario(actual if status == "PASS" else expected)
        self.make_executable()

        def fake_process(executable, scenario, run_dir, env, timeout):
            repository = run_dir / "repository"
            project = repository / ".retrolarium/ju/smb1-ju.rlj"
            project.parent.mkdir(parents=True)
            project.write_text("synthetic project", encoding="utf-8")
            (repository / self.scenario["source"]["descriptor"]).write_text("synthetic descriptor", encoding="utf-8")
            result = {
                "status": status,
                "last_step": "synthetic native result",
                "stop_reason": "synthetic unit-test result",
                "actual_sha256": actual if status in ("PASS", "BASELINE_PENDING") else None,
                "project_path": str(project),
            }
            (run_dir / "result.json").write_text(json.dumps(result), encoding="utf-8")
            return {
                "exit": exit_code,
                "timed_out": False,
                "wall_seconds": 0,
                "stdout": "",
                "stderr": "",
            }

        with mock.patch.object(runner, "run_process", side_effect=fake_process) as process:
            with (
                contextlib.redirect_stdout(io.StringIO()),
                contextlib.redirect_stderr(io.StringIO()),
            ):
                arguments = self.arguments()
                if source_archive is not None:
                    arguments.extend(("--source-archive", str(source_archive)))
                code = runner.main(arguments)
        self.assertEqual(1, process.call_count)
        reports = list(self.out.glob("*/report.json"))
        self.assertEqual(1, len(reports))
        report = json.loads(reports[0].read_text(encoding="utf-8"))
        self.assertTrue(reports[0].with_name("report.html").is_file())
        return code, report

    def test_msvc_configuration_executables_are_found_from_the_build_root(self):
        name = "retrolarium-gui-regression.exe" if os.name == "nt" else "retrolarium-gui-regression"
        for configuration in ("Release", "RelWithDebInfo", "MinSizeRel", "Debug"):
            for nested in (False, True):
                with self.subTest(configuration=configuration, nested=nested):
                    build = self.root / f"{configuration}-{nested}"
                    parent = build / configuration
                    if nested:
                        parent = build / ".cmake-build" / configuration
                    parent.mkdir(parents=True)
                    executable = parent / name
                    executable.write_bytes(b"test double")
                    self.assertEqual(executable, runner.studio_executable(build))
                    self.assertEqual(executable, runner.studio_executable(parent))

    def test_renamed_rom_is_selected_by_content_not_name_or_extension(self):
        (self.roms / "Super Mario Bros. (JU) [!].nes").write_bytes(b"wrong content")
        wanted = self.roms / "nested" / "backup-without-a-game-name.bin"
        wanted.parent.mkdir()
        wanted.write_bytes(self.rom_bytes)
        self.assertEqual(wanted, runner.find_rom(self.roms, self.input_sha256))

    def test_missing_and_duplicate_exact_hashes_are_unambiguous_input_errors(self):
        with self.assertRaisesRegex(runner.InputError, self.input_sha256):
            runner.find_rom(self.roms, self.input_sha256)
        for name in ("one.nes", "two.anything"):
            (self.roms / name).write_bytes(self.rom_bytes)
        with self.assertRaisesRegex(runner.InputError, "ambiguous"):
            runner.find_rom(self.roms, self.input_sha256)

    def test_source_environment_is_isolated_and_has_no_trust_overrides(self):
        inherited = {
            "RETROLARIUM_TEST_HOOKS": "1",
            "RETROLARIUM_DEV": "0",
            "RETROLARIUM_KIT": "wrong-kit",
            "RETROLARIUM_UNVERIFIED_GAMES": "1",
            "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS": "all",
            "RETROLARIUM_GAMES_SOURCE": "wrong-source",
        }
        with mock.patch.dict(os.environ, inherited):
            env = runner.environment(self.studio, self.root / "home", self.root / "tmp", self.root / "kit")
        self.assertEqual("1", env["RETROLARIUM_DEV"])
        self.assertEqual(str(self.root / "kit/rltool.py"), env["RETROLARIUM_KIT"])
        self.assertEqual(
            (self.root / "home/verified").resolve(),
            Path(env["RETROLARIUM_VERIFIED_HOME"]),
        )
        for key in (
            "RETROLARIUM_TEST_HOOKS",
            "RETROLARIUM_UNVERIFIED_GAMES",
            "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS",
            "RETROLARIUM_GAMES_SOURCE",
        ):
            self.assertNotIn(key, env)
            self.assertEqual("<absent>", runner.environment_report(env)[key])
        self.assertEqual((self.root / "home").resolve(), Path(env["RETROLARIUM_STUDIO_HOME"]))
        self.assertEqual((self.root / "home").resolve(), Path(env["HOME"]))
        self.assertEqual((self.root / "tmp").resolve(), Path(env["TEMP"]))

    def test_explicit_source_archive_replaces_only_the_inherited_source_override(self):
        archive = self.root / "pinned-source.zip"
        archive.write_bytes(b"synthetic archive")
        inherited = {
            "RETROLARIUM_GAMES_SOURCE": "wrong-source",
            "RETROLARIUM_UNVERIFIED_GAMES": "1",
            "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS": "all",
        }
        with mock.patch.dict(os.environ, inherited):
            env = runner.environment(
                self.studio,
                self.root / "home",
                self.root / "tmp",
                self.root / "kit",
                archive,
            )
        self.assertEqual(str(archive.resolve()), env["RETROLARIUM_GAMES_SOURCE"])
        self.assertNotIn("RETROLARIUM_UNVERIFIED_GAMES", env)
        self.assertNotIn("RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS", env)
        self.assertEqual(str(archive.resolve()), runner.environment_report(env)["RETROLARIUM_GAMES_SOURCE"])

    def test_packaged_environment_cannot_inherit_source_tools_or_trust_overrides(self):
        (self.studio / "kit").mkdir()
        (self.studio / "kit/rltool.py").write_text("", encoding="utf-8")
        inherited = {
            "RETROLARIUM_TEST_HOOKS": "1",
            "RETROLARIUM_DEV": "1",
            "RETROLARIUM_KIT": "source-kit",
            "RETROLARIUM_UNVERIFIED_GAMES": "1",
            "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS": "all",
            "RETROLARIUM_GAMES_SOURCE": "wrong-source",
        }
        with mock.patch.dict(os.environ, inherited):
            env = runner.environment(self.studio, self.root / "home", self.root / "tmp")
        self.assertNotIn("RETROLARIUM_DEV", env)
        self.assertNotIn("RETROLARIUM_KIT", env)
        for key in (
            "RETROLARIUM_TEST_HOOKS",
            "RETROLARIUM_UNVERIFIED_GAMES",
            "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS",
            "RETROLARIUM_GAMES_SOURCE",
        ):
            self.assertNotIn(key, env)

    def test_every_run_reserves_a_new_directory(self):
        first = runner.new_run_directory(self.out, "same-scenario")
        second = runner.new_run_directory(self.out, "same-scenario")
        self.assertNotEqual(first, second)
        self.assertTrue(first.is_dir())
        self.assertTrue(second.is_dir())

    def test_missing_input_writes_reports_and_returns_two_without_a_rom(self):
        self.write_scenario()
        with (
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            code = runner.main(self.arguments())
        self.assertEqual(2, code)
        report_path = next(self.out.glob("*/report.json"))
        report = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual("MISSING_INPUT", report["status"])
        self.assertIn(self.scenario["revision_name"], report["stop_reason"])
        self.assertIn(self.input_sha256, report["stop_reason"])
        self.assertTrue(report_path.with_name("report.html").is_file())

    def test_native_statuses_map_to_the_documented_exit_codes(self):
        for status, exit_code in (("PASS", 0), ("FAIL", 1), ("BASELINE_PENDING", 3)):
            with self.subTest(status=status):
                temporary = tempfile.TemporaryDirectory()
                self.addCleanup(temporary.cleanup)
                self.root = Path(temporary.name)
                self.roms = self.root / "roms"
                self.roms.mkdir()
                self.out = self.root / "out"
                self.studio = self.root / "studio"
                self.studio.mkdir()
                self.scenario_path = self.root / "scenario.json"
                expected = None if status == "BASELINE_PENDING" else "unchanged"
                code, report = self.invoke_with_native_result(status, exit_code, expected)
                self.assertEqual(exit_code, code)
                self.assertEqual(status, report["status"])
                self.assertEqual(0 if status == "FAIL" else 1, self.verification.call_count)

    def test_success_verifies_repository_with_selected_kit_and_isolated_environment(
        self,
    ):
        code, report = self.invoke_with_native_result("BASELINE_PENDING", 3, None)
        self.assertEqual(3, code)
        kit, repository, scenario, env = self.verification.call_args.args
        self.assertEqual((self.root / "kit").resolve(), kit)
        self.assertEqual(Path(report["game_source"]["repository_path"]), repository)
        self.assertEqual(self.scenario["source"]["commit"], scenario["source"]["commit"])
        run_dir = Path(report["run_directory"])
        self.assertEqual(run_dir / "home/verified", Path(env["RETROLARIUM_VERIFIED_HOME"]))
        self.assertEqual("synthetic verified source", report["game_source"]["verification"]["method"])

    def test_explicit_source_archive_is_passed_to_native_and_recorded(self):
        archive = self.root / "pinned-source.zip"
        archive.write_bytes(b"synthetic archive")
        code, report = self.invoke_with_native_result("BASELINE_PENDING", 3, None, archive)
        self.assertEqual(3, code)
        env = self.verification.call_args.args[3]
        self.assertEqual(str(archive.resolve()), env["RETROLARIUM_GAMES_SOURCE"])
        self.assertEqual(str(archive.resolve()), report["environment"]["RETROLARIUM_GAMES_SOURCE"])
        self.assertEqual(str(archive.resolve()), report["game_source"]["archive"]["path"])
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), report["game_source"]["archive"]["sha256"])
        self.assertFalse(report["game_source"]["network_download_covered"])

    def test_source_verification_error_changes_completed_build_to_fail(self):
        self.verification.side_effect = RuntimeError("synthetic source pin mismatch")
        code, report = self.invoke_with_native_result("BASELINE_PENDING", 3, None)
        self.assertEqual(1, code)
        self.assertEqual("FAIL", report["status"])
        self.assertIn("synthetic source pin mismatch", report["stop_reason"])
        self.assertEqual("verify the prepared source with the selected Kit", report["last_step"])

    def test_effective_private_paths_stay_inside_the_run_directory(self):
        code, report = self.invoke_with_native_result("BASELINE_PENDING", 3, None)
        self.assertEqual(3, code)
        run_dir = Path(report["run_directory"])
        effective = json.loads((run_dir / "scenario.effective.json").read_text(encoding="utf-8"))
        self.assertEqual(str(run_dir), effective["output"])
        self.assertEqual((self.roms / "renamed.data").resolve(), Path(effective["rom_path"]))
        original = json.loads(self.scenario_path.read_text(encoding="utf-8"))
        self.assertNotIn("rom_path", original)
        self.assertNotIn("output", original)


class SourceVerificationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.kit = self.root / "selected-kit"
        self.repository = self.root / "repository"
        self.verified_home = self.root / "verified"
        self.record = self.verified_home / "record.json"
        self.record.parent.mkdir(parents=True)
        self.record.write_text("synthetic record", encoding="utf-8")
        self.scenario = json.loads((ROOT / "tools/gui_regression/smb1_ju_block.json").read_text(encoding="utf-8"))
        self.env = {"RETROLARIUM_VERIFIED_HOME": str(self.verified_home)}

    def evidence(self):
        return {
            "record_path": str(self.record),
            "root": str(self.repository.resolve()),
            "game": self.scenario["game"],
            "repository": self.scenario["source"]["repository"],
            "commit": self.scenario["source"]["commit"],
            "content_sha256": self.scenario["source"]["content_sha256"],
            "url": "https://example.invalid/source.git",
            "script_count": 2,
            "scripts_sha256": "b" * 64,
        }

    def test_verification_uses_selected_kit_sources_and_runner_environment(self):
        completed = mock.Mock(returncode=0, stdout=json.dumps(self.evidence()), stderr="")
        with mock.patch.object(runner.subprocess, "run", return_value=completed) as run:
            result = runner.verify_game_source(self.kit, self.repository, self.scenario, self.env)
        command = run.call_args.args[0]
        self.assertEqual(str(self.kit.resolve() / "src"), command[4])
        self.assertEqual(str(self.repository.resolve()), command[5])
        self.assertIs(self.env, run.call_args.kwargs["env"])
        self.assertEqual(str(self.kit.resolve()), result["kit"])
        self.assertEqual(self.scenario["source"]["commit"], result["commit"])
        self.assertTrue(result["record"]["exists"])

    def test_verification_rejects_a_commit_or_content_mismatch(self):
        for key in ("commit", "content_sha256"):
            with self.subTest(key=key):
                evidence = self.evidence()
                evidence[key] = "0" * len(evidence[key])
                completed = mock.Mock(returncode=0, stdout=json.dumps(evidence), stderr="")
                with mock.patch.object(runner.subprocess, "run", return_value=completed):
                    with self.assertRaisesRegex(RuntimeError, key):
                        runner.verify_game_source(self.kit, self.repository, self.scenario, self.env)

    def test_verification_propagates_a_missing_marker_or_changed_script_refusal(self):
        completed = mock.Mock(returncode=1, stdout="", stderr="Kit verification record is missing")
        with mock.patch.object(runner.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(RuntimeError, "record is missing"):
                runner.verify_game_source(self.kit, self.repository, self.scenario, self.env)


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.studio = self.root / "studio"
        self.studio.mkdir()
        self.selected_kit = self.root / "selected-kit"

    def write_manifest(self, kit, release):
        path = kit / "src/retrolarium_kit/core/toolchains/manifest.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps({"release": release, "components": {}, "suites": {}}),
            encoding="utf-8",
        )

    def identity(self, path):
        return {"path": str(path.resolve())}

    def test_source_provenance_uses_the_selected_kit_not_the_default_sibling(self):
        self.write_manifest(self.selected_kit, "selected-kit-release")
        with (
            mock.patch.object(runner, "git_identity", side_effect=self.identity),
            mock.patch.object(runner, "studio_version", return_value="test"),
        ):
            result = runner.provenance(self.studio, self.selected_kit.resolve())
        self.assertEqual(str(self.selected_kit.resolve()), result["sources"]["kit"]["path"])
        self.assertEqual("selected-kit-release", result["toolchains"]["release"])
        self.assertIn(str(self.selected_kit.resolve()), result["toolchains"]["manifest"]["path"])

    def test_packaged_provenance_uses_only_the_bundled_kit(self):
        bundled = self.studio / "kit"
        bundled.mkdir()
        (bundled / "rltool.py").write_text("", encoding="utf-8")
        self.write_manifest(bundled, "bundled-release")
        self.write_manifest(self.selected_kit, "external-release")
        kit = runner.effective_kit(self.studio, self.selected_kit)
        with (
            mock.patch.object(runner, "git_identity", side_effect=self.identity),
            mock.patch.object(runner, "studio_version", return_value="test"),
        ):
            result = runner.provenance(self.studio, kit)
        self.assertEqual(bundled.resolve(), kit)
        self.assertEqual(str(bundled.resolve()), result["sources"]["kit"]["path"])
        self.assertEqual("bundled-release", result["toolchains"]["release"])


if __name__ == "__main__":
    unittest.main()
