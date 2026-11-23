"""Public synthetic data, local-only integration data and their separation."""

import importlib.util
import json
import io
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = load("release")
local = load("test_smb_rom")


class SmbFixtureTests(unittest.TestCase):
    def test_public_fixtures_are_present_and_explicitly_synthetic(self) -> None:
        directory = ROOT / "tests/fixtures/smb"
        for name in ("levels.json", "screens.json", "music.json"):
            data = (directory / name).read_bytes()
            root = json.loads(data)
            self.assertEqual("synthetic", root["provenance"]["kind"])
            self.assertNotIn("rom_sha256", root["provenance"])
            self.assertFalse(release.private_game_data(name, data))
        # The local command must never claim a synthetic comparison covered the ROM.
        with self.assertRaisesRegex(ValueError, "ROM-derived"):
            local.validate_corpus(directory)

    def test_git_inputs_contain_no_private_corpora_or_roms(self) -> None:
        # Include untracked publication candidates too. Ignored build/ corpora
        # stay local; force-adding one makes it visible here and fails CI.
        names = (
            subprocess.check_output(
                ["git", "-C", str(ROOT), "ls-files", "--cached", "--others", "--exclude-standard", "-z"]
            )
            .decode("utf-8")
            .split("\0")
        )
        for name in filter(None, names):
            path = ROOT / name
            if path.is_file():
                with self.subTest(name=name):
                    self.assertFalse(
                        release.private_game_data(name, path.read_bytes() if path.suffix.lower() == ".json" else None)
                    )

    def make_local(self, directory):
        for name in ("levels.json", "screens.json", "music.json"):
            (directory / name).write_text(
                json.dumps({"profile": "smb1.ju", "provenance": {"kind": "rom-derived", "rom_sha256": "a" * 64}})
            )

    def test_local_integration_requires_three_matching_corpora(self) -> None:
        with tempfile.TemporaryDirectory(prefix="SMB corpora with spaces ") as temporary:
            directory = Path(temporary)
            with self.assertRaises(OSError):
                local.validate_corpus(directory)
            self.make_local(directory)
            local.validate_corpus(directory)
            music = json.loads((directory / "music.json").read_text())
            music["provenance"]["rom_sha256"] = "b" * 64
            (directory / "music.json").write_text(json.dumps(music))
            with self.assertRaisesRegex(ValueError, "different ROMs"):
                local.validate_corpus(directory)

    def test_local_integration_requires_executables_and_counts_a_skip_as_failure(self) -> None:
        with tempfile.TemporaryDirectory(prefix="SMB integration with spaces ") as temporary:
            directory = Path(temporary)
            self.make_local(directory)
            with self.assertRaisesRegex(ValueError, "missing test program"):
                local.run_suite(directory, directory, {}, 1)
            for name in local.CASES:
                (directory / (name + (".exe" if local.os.name == "nt" else ""))).touch()
            output = io.StringIO()
            with (
                patch.object(local.subprocess, "run", return_value=subprocess.CompletedProcess([], 77)) as run,
                redirect_stdout(output),
                redirect_stderr(output),
            ):
                self.assertEqual(1, local.run_suite(directory, directory, {}, 1))
                self.assertEqual(4, run.call_count)
            self.assertIn("0 passed, 4 failed", output.getvalue())


if __name__ == "__main__":
    unittest.main()
