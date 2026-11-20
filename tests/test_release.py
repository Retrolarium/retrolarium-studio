from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

SPEC = importlib.util.spec_from_file_location(
    "studio_release", Path(__file__).resolve().parents[1] / "tools/release.py"
)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class NativeProvenanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = {"Studio": {"commit": "1" * 40, "changes": None}, "Core": {"commit": "2" * 40, "changes": None}}
        self.patch = mock.patch.object(release, "sources", return_value=self.state)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        for name in release.NATIVE_FILES:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        self.source = self.root / "source.json"
        self.source.write_text(json.dumps(self.state))
        release.record_build(self.root, self.source)

    def test_rejects_an_old_native_source_commit(self) -> None:
        with mock.patch.object(
            release, "sources", return_value={**self.state, "Studio": {"commit": "3" * 40, "changes": None}}
        ):
            with self.assertRaisesRegex(ValueError, "current Studio"):
                release.verify_build(self.root)

    def test_rejects_a_replaced_helper(self) -> None:
        (self.root / "tools/rlcore.exe").write_bytes(b"older binary")
        with self.assertRaisesRegex(ValueError, "rlcore.exe"):
            release.verify_build(self.root)

    def test_dirty_build_is_only_available_for_trial_packages(self) -> None:
        self.state["Studio"]["changes"] = "4" * 64
        self.source.write_text(json.dumps(self.state))
        release.record_build(self.root, self.source)
        with self.assertRaisesRegex(ValueError, "uncommitted"):
            release.verify_build(self.root)
        release.verify_build(self.root, allow_dirty=True)

    def test_records_refuse_sources_changed_during_build(self) -> None:
        self.state["Core"]["commit"] = "5" * 40
        with self.assertRaisesRegex(ValueError, "during the native build"):
            release.record_build(self.root, self.source)

    def test_release_hashes_describe_the_written_artifacts(self) -> None:
        (self.root / "CMakeLists.txt").write_text("project(RetrolariumStudio VERSION 0.2.0)")
        (self.root / "RetrolariumStudio-0.2.0-setup.exe").write_bytes(b"setup payload")
        archive = self.root / "RetrolariumStudio-0.2.0-win64.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("RetrolariumStudio/BUILD-INFO.txt", "Retrolarium Studio 0.2.0\nKit: pinned\n")
            bundle.write(self.root / "native-build.json", "RetrolariumStudio/native-build.json")
            for name in release.NATIVE_FILES:
                if name != "retrolarium-setup.exe":
                    bundle.write(self.root / name, "RetrolariumStudio/" + name)
        with mock.patch.object(release, "STUDIO", self.root):
            release.release_notes(self.root)
        checksums = (self.root / "SHA256SUMS.txt").read_text()
        self.assertIn(release.digest(archive), checksums)
        self.assertIn(release.digest(self.root / "RetrolariumStudio-0.2.0-setup.exe"), checksums)
        self.assertIn(checksums.splitlines()[0].split()[0], (self.root / "release-notes.md").read_text())

    def test_release_rejects_a_zip_with_different_native_files(self) -> None:
        (self.root / "CMakeLists.txt").write_text("project(RetrolariumStudio VERSION 0.2.0)")
        archive = self.root / "RetrolariumStudio-0.2.0-win64.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("RetrolariumStudio/BUILD-INFO.txt", "Retrolarium Studio 0.2.0")
            bundle.write(self.root / "native-build.json", "RetrolariumStudio/native-build.json")
            bundle.writestr("RetrolariumStudio/retrolarium-studio.exe", b"old native Studio")
        with mock.patch.object(release, "STUDIO", self.root):
            with self.assertRaisesRegex(ValueError, "different native"):
                release.release_notes(self.root)


if __name__ == "__main__":
    unittest.main()
