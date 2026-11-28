"""Functional inventory/mode tests; no ROMs, GUI executables or security suites."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "tools/gui_regression"
sys.path.insert(0, str(FOLDER))
try:

    def module(name):
        spec = importlib.util.spec_from_file_location("gui_" + name, FOLDER / (name + ".py"))
        result = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(result)
        return result

    inventory = module("inventory")
    inspection = module("inspect_game")
finally:
    sys.path.pop(0)


class GuiInventoryTests(unittest.TestCase):
    def test_coverage_declares_every_supported_revision(self):
        catalog = json.loads(
            (ROOT.parent / "retrolarium-kit/src/retrolarium_kit/core/profiles/repositories.json").read_text()
        )
        matrix = inventory.load_coverage(FOLDER / "coverage.json", catalog)
        self.assertEqual({"demo", "playtest"}, set(matrix["excluded_kinds"]))
        self.assertEqual(
            {(game, rev["id"]) for game, source in catalog["games"].items() for rev in source["revisions"]},
            {(row["game"], row["revision"]) for row in matrix["revisions"]},
        )

    def test_new_catalog_revision_requires_explicit_coverage_decision(self):
        catalog = json.loads(
            (ROOT.parent / "retrolarium-kit/src/retrolarium_kit/core/profiles/repositories.json").read_text()
        )
        catalog["games"]["smb1"]["revisions"].append(dict(id="new-revision", sha256="a" * 64))
        with self.assertRaisesRegex(ValueError, "every catalog game/revision"):
            inventory.load_coverage(FOLDER / "coverage.json", catalog)

    def test_cannot_exclude_editable_tab_from_catalog_policy(self):
        catalog = {"games": {"sample": {"revisions": [{"id": "one", "sha256": "a" * 64}]}}}
        matrix = dict(
            schema="retrolarium.gui-coverage",
            schema_version=1,
            excluded_kinds=["demo", "playtest", "world"],
            revisions=[dict(game="sample", revision="one", input_sha256="a" * 64, required_kinds=["text"], suite=None)],
        )
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "coverage.json"
            path.write_text(json.dumps(matrix))
            with self.assertRaisesRegex(ValueError, "Demo and Playtest"):
                inventory.load_coverage(path, catalog)

    def test_input_without_scenarios_is_incomplete(self):
        row = dict(game="sample", revision="one", input_sha256="a" * 64, suite=None, required_kinds=["text"])
        result = inventory.audit(Path("unused"), {"revisions": [row]}, {"a" * 64: ["synthetic-input"]}, [])
        self.assertEqual("INCOMPLETE", result["status"])
        self.assertEqual("NOT_IMPLEMENTED", result["revisions"][0]["status"])
        self.assertEqual(0, result["complete_revisions"])

    def test_duplicate_report_of_one_project_is_only_one_clean_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = root / "synthetic.bin"
            artifact.write_bytes(b"synthetic functional artifact")
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            scenario = dict(
                id="sample",
                expected_sha256=digest,
                input_sha256="a" * 64,
                project={},
                edit={},
                source=dict(commit="b" * 40, content_sha256="c" * 64),
                expected_kinds=["text"],
                switch_documents=["all"],
            )
            report = dict(
                status="PASS",
                undo_redo_verified=True,
                library_layout_contract=2,
                scenario_id="sample",
                input_sha256="a" * 64,
                actual_sha256=digest,
                scenario=copy.deepcopy(scenario),
                game_source=dict(verification=scenario["source"]),
                artifact_path=str(artifact),
                project_path=str(root / "one-project.rlj"),
            )
            reports = [(root / f"report-{i}.json", copy.deepcopy(report)) for i in range(3)]
            self.assertEqual(1, len(inventory.clean_passes(scenario, reports)))
            for _, recorded in reports:
                recorded["undo_redo_verified"] = False
            self.assertEqual([], inventory.clean_passes(scenario, reports))
            scenario["mirrored_documents"] = ["shared-table"]
            for _, recorded in reports:
                recorded["undo_redo_verified"] = True
                recorded["scenario"]["mirrored_documents"] = ["shared-table"]
            self.assertEqual([], inventory.clean_passes(scenario, reports))
            for _, recorded in reports:
                recorded["mirrored_documents_verified"] = ["shared-table"]
            self.assertEqual(1, len(inventory.clean_passes(scenario, reports)))
            for _, recorded in reports:
                recorded["status"] = "INSPECTED"
            self.assertEqual([], inventory.clean_passes(scenario, reports))

    def test_failure_history_remains_visible_and_never_supplies_a_missing_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = b"synthetic functional artifact"
            digest = hashlib.sha256(payload).hexdigest()
            scenario = dict(
                id="sample",
                expected_sha256=digest,
                input_sha256="a" * 64,
                project=dict(kind="text"),
                edit={},
                source=dict(commit="b" * 40, content_sha256="c" * 64),
                expected_kinds=["text"],
                switch_documents=[],
            )
            reports = []
            for index in range(3):
                project = root / f"project-{index}"
                project.mkdir()
                artifact = project / "synthetic.bin"
                artifact.write_bytes(payload)
                reports.append(
                    (
                        project / "report.json",
                        dict(
                            status="PASS",
                            undo_redo_verified=True,
                            library_layout_contract=2,
                            scenario_id="sample",
                            input_sha256="a" * 64,
                            actual_sha256=digest,
                            scenario=copy.deepcopy(scenario),
                            game_source=dict(verification=scenario["source"]),
                            artifact_path=str(artifact),
                            project_path=str(project / "sample.rlj"),
                        ),
                    )
                )
            failure = dict(reports[0][1], status="FAIL", stop_reason="synthetic process failure")
            failed = (root / "failed-report.json", failure)
            row = dict(
                game="sample", revision="one", input_sha256="a" * 64, suite="sample.json", required_kinds=["text"]
            )
            with mock.patch.object(
                inventory.run_suite, "load_matrix", return_value=({}, [(root / "case.json", scenario)])
            ):
                incomplete = inventory.audit(
                    root / "matrix.json", {"revisions": [row]}, {"a" * 64: ["input"]}, reports[:2] + [failed]
                )
                complete = inventory.audit(
                    root / "matrix.json", {"revisions": [row]}, {"a" * 64: ["input"]}, reports + [failed]
                )
            self.assertEqual("RUNS_REQUIRED", incomplete["revisions"][0]["status"])
            self.assertEqual("COMPLETE", complete["status"])
            self.assertEqual(3, complete["revisions"][0]["scenarios"][0]["clean_passes"])
            self.assertEqual(1, complete["historical_failed_attempts"])
            self.assertEqual("FAIL", failure["status"])
            inventory.write_reports(root / "audit", complete)
            page = (root / "audit/coverage.html").read_text()
            self.assertIn("Historical failed attempts: 1", page)
            self.assertIn("synthetic process failure", page)
            self.assertIn("failed-report.json", page)

    def test_inspection_is_not_a_normal_regression_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom = root / "synthetic.bin"
            rom.write_bytes(b"not a game ROM")
            report = dict(
                status="INSPECTED",
                input_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                inventory=[dict(kind="text")],
            )
            process = dict(exit=4, timed_out=False)
            normal = copy.deepcopy(report)
            inventory.run.validate_result(normal, process, None, rom, root)
            self.assertEqual("FAIL", normal["status"])
            inventory.run.validate_result(report, process, None, rom, root, inspect=True)
            self.assertEqual("INSPECTED", report["status"])
            self.assertEqual("FAIL", inventory.run_suite.aggregate_status([dict(status="INSPECTED")]))

    def test_rom_container_size_is_part_of_acceptance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original = root / "synthetic-input.bin"
            original.write_bytes(b"synthetic original container and extra payload")
            artifact = root / "synthetic-output.bin"
            artifact.write_bytes(b"synthetic edited payload")
            digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
            report = dict(
                status="PASS",
                input_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                actual_sha256=digest,
                artifact_path=str(artifact),
            )
            complete = copy.deepcopy(report)
            inventory.run.validate_result(
                report, dict(exit=0, timed_out=False), digest, original, root, artifact_size=len(original.read_bytes())
            )
            self.assertEqual("FAIL", report["status"])
            self.assertIn("container", report["stop_reason"])
            complete.pop("artifact_path")
            inventory.run.validate_result(
                complete,
                dict(exit=0, timed_out=False),
                digest,
                original,
                root,
                artifact_size=len(original.read_bytes()),
            )
            self.assertEqual("FAIL", complete["status"])
            self.assertIn("artifact", complete["stop_reason"])

    def test_declared_coupled_change_rejects_ambiguous_or_unpinned_expectations(self):
        source = FOLDER / "castle_excellent_castlequest_usa_ending_text.json"
        valid = inventory.run.load_scenario(source)
        change = valid["edit"]["state_changes"][0]
        self.assertEqual(["cells", "1", "2"], change["path"])
        self.assertNotEqual(change["old"], change["new"])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "scenario.json"
            for changes in (
                [change, change],
                [{"path": change["path"], "new": "unreviewed"}],
                [dict(change, path=[])],
                [dict(change, new=change["old"])],
                "infer-from-output",
            ):
                with self.subTest(changes=changes):
                    scenario = copy.deepcopy(valid)
                    scenario["edit"]["state_changes"] = changes
                    path.write_text(json.dumps(scenario), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        inventory.run.load_scenario(path)

    def test_current_geometry_contract_rejects_old_or_failed_library_checks(self):
        self.assertFalse(inventory.run.layout_verification_complete({}))
        self.assertFalse(inventory.run.layout_verification_complete(dict(library_layout_contract=1)))
        self.assertTrue(inventory.run.layout_verification_complete(dict(library_layout_contract=2)))
        check = dict(kind="grid", card_count=2048, overlap_free=True, error="")
        report = dict(library_layout_contract=2, library_layout_checks=[check])
        self.assertTrue(inventory.run.layout_verification_complete(report))
        for invalid in (dict(check, overlap_free=False), dict(check, error="cell clipped"), "unverified"):
            with self.subTest(invalid=invalid):
                self.assertFalse(
                    inventory.run.layout_verification_complete(dict(report, library_layout_checks=[invalid]))
                )

    def test_object_translations_are_fixed_nonzero_and_unambiguous(self):
        valid = inventory.run.load_scenario(FOLDER / "flicky_ue_title_sprite_position.json")
        position = valid["edit"]["object_positions"][0]
        self.assertEqual(dict(id="sprite:statics:0", dx=1, dy=0), position)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "scenario.json"
            for positions in (
                [position, position],
                [dict(position, dx=0)],
                [dict(position, dx="infer")],
                [dict(position, id="")],
                "learn-after-edit",
            ):
                with self.subTest(positions=positions):
                    scenario = copy.deepcopy(valid)
                    scenario["edit"]["object_positions"] = positions
                    path.write_text(json.dumps(scenario), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        inventory.run.load_scenario(path)

    def test_toolchain_evidence_includes_megadrive_and_hashes_its_local_tools(self):
        with tempfile.TemporaryDirectory() as temporary:
            kit = Path(temporary)
            base = kit / "src/retrolarium_kit/core/toolchains"
            tool = base / "payloads/host/asl"
            tool.parent.mkdir(parents=True)
            tool.write_bytes(b"synthetic assembler")
            digest = hashlib.sha256(tool.read_bytes()).hexdigest()
            host = "windows-x86_64" if inventory.run.os.name == "nt" else "linux-x86_64"
            manifest = dict(
                release="functional-test",
                suites=dict(nes=dict(components=["cc65"]), megadrive=dict(components=["asl"])),
                components=dict(
                    cc65=dict(version="nes"),
                    asl=dict(
                        version="md",
                        hosts={
                            host: dict(files=[dict(package_path="payloads/host/asl", executable=True, sha256=digest)])
                        },
                    ),
                ),
            )
            (base / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            evidence = inventory.run.toolchain_evidence(kit)
            self.assertEqual({"nes", "megadrive"}, set(evidence["suites"]))
            recorded = evidence["suites"]["megadrive"][0]["executables"][0]
            self.assertEqual(digest, recorded["sha256"])
            self.assertEqual(digest, recorded["expected_sha256"])
            self.assertEqual(str(tool), recorded["path"])

    def test_palette_model_result_requires_visible_widget_evidence(self):
        report = dict(library_layout_contract=2, scenario=dict(project=dict(kind="palettes", editor="PaletteEditor")))
        self.assertFalse(inventory.run.presentation_verification_complete(report))
        self.assertTrue(
            inventory.run.presentation_verification_complete(dict(report, palette_presentation_verified=True))
        )
        self.assertFalse(
            inventory.run.presentation_verification_complete(dict(report, palette_presentation_verified=False))
        )

    def test_source_build_resolves_its_matching_core(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            studio = root / "studio" / "Release"
            name = "rlcore.exe" if inventory.run.os.name == "nt" else "rlcore"
            core = studio.parent / "retrolarium_core" / "Release" / name
            with mock.patch.dict(inventory.run.os.environ, {}, clear=True):
                with mock.patch.object(Path, "is_file", autospec=True, side_effect=lambda path: path == core):
                    env = inventory.run.environment(studio, root / "home", root / "tmp", root / "kit")
            self.assertEqual(str(core.resolve()), env["RETROLARIUM_CORE"])

    def test_explicit_core_choice_takes_precedence_over_build_discovery(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            explicit = str(root / "selected-core")
            with mock.patch.dict(inventory.run.os.environ, {"RETROLARIUM_CORE": explicit}):
                with mock.patch.object(inventory.run, "build_core_executable") as discover:
                    env = inventory.run.environment(root / "studio", root / "home", root / "tmp", root / "kit")
            self.assertEqual(explicit, env["RETROLARIUM_CORE"])
            discover.assert_not_called()

    def test_inspection_wrapper_uses_the_requested_revision_without_a_golden(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.object(inspection.run, "main", return_value=4) as child:
                code = inspection.main(
                    [
                        "--game",
                        "smb1",
                        "--revision",
                        "pal",
                        "--roms",
                        str(root / "inputs"),
                        "--studio",
                        str(root / "studio"),
                        "--kit",
                        str(ROOT.parent / "retrolarium-kit"),
                        "--out",
                        str(root / "reports"),
                        "--document",
                        "world=physics",
                    ]
                )
            self.assertEqual(4, code)
            command = child.call_args.args[0]
            scenario = json.loads(Path(command[command.index("--scenario") + 1]).read_text())
            self.assertEqual("pal", scenario["revision"])
            self.assertIsNone(scenario["expected_sha256"])
            self.assertEqual({"operation": "inspect"}, scenario["edit"])
            self.assertEqual({"world": ["physics"]}, scenario["inspect_documents"])


if __name__ == "__main__":
    unittest.main()
