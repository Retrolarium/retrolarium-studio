from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zipfile
import zlib

SPEC = importlib.util.spec_from_file_location(
    "studio_release", Path(__file__).resolve().parents[1] / "tools/release.py"
)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)

CHANGELOG = """# Changelog

## 0.3.0

Not this one.

## 0.2.0

The release's own words.

### Details

Kept with it.

## 0.1.0

Older.
"""


def make_setup(path: Path, package: Path, version: str) -> None:
    """A setup program as InstallerPackage::create writes it: a stub, every
    file qCompress-ed, the JSON manifest and the footer."""
    data = bytearray(b"MZ setup stub")
    stub_size = len(data)
    files = []
    for item in sorted(p for p in package.rglob("*") if p.is_file()):
        plain = item.read_bytes()
        packed = len(plain).to_bytes(4, "big") + zlib.compress(plain, 9)
        files.append(
            {
                "path": item.relative_to(package).as_posix(),
                "offset": len(data),
                "size": len(plain),
                "packedSize": len(packed),
                "sha256": hashlib.sha256(plain).hexdigest(),
            }
        )
        data += packed
    manifest = json.dumps({"format": 1, "version": version, "stubSize": stub_size, "files": files}).encode()
    offset = len(data)
    data += manifest
    data += release.PAYLOAD_MAGIC + struct.pack("<QQ", offset, len(manifest))
    path.write_bytes(bytes(data))


class NativeProvenanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = {"Studio": {"commit": "1" * 40, "changes": None}, "Core": {"commit": "2" * 40, "changes": None}}
        self.patch = mock.patch.object(release, "sources", return_value=self.state)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        core_pin = mock.patch.object(release, "verify_core_ref")
        core_pin.start()
        self.addCleanup(core_pin.stop)
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

    def make_package(self) -> Path:
        package = self.root / "out/RetrolariumStudio"
        (package / "tools").mkdir(parents=True)
        (package / "mame/roms/fds").mkdir(parents=True)
        (package / "BUILD-INFO.txt").write_text("Retrolarium Studio 0.2.0\nKit: pinned\n")
        (package / "native-build.json").write_bytes((self.root / "native-build.json").read_bytes())
        # Real reviewed notice texts, with synthetic source identities.
        notices = release.qt_notices()
        record = json.loads((package / "native-build.json").read_text())
        record["qt_image"] = notices["reviewed_sdk_image"]
        (package / "native-build.json").write_text(json.dumps(record))
        patcher = mock.patch.object(release, "qt_notices", return_value=notices)
        patcher.start()
        self.addCleanup(patcher.stop)
        notice_folder = package / "licenses/qt-3rdparty"
        notice_folder.mkdir(parents=True)
        for entry in notices["components"]:
            source = Path(release.__file__).resolve().parents[1] / "resources/licenses/qt-3rdparty" / entry["notice"]
            (notice_folder / entry["notice"]).write_bytes(source.read_bytes())
        (package / "README.txt").write_text(
            "Retrolarium Studio 0.2.0\n"
            + "\n".join(
                "https://github.com/" + repository + "/tree/" + "a" * 40
                for repository in (
                    "Retrolarium/retrolarium-studio",
                    "Retrolarium/retrolarium-kit",
                    "Retrolarium/retrolarium-core",
                    "OpenKaryon/longreach-qt",
                    "mamedev/mame",
                )
            )
        )
        for name in (*release.REQUIRED_LICENSES, "THIRD-PARTY.txt"):
            path = package / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Synthetic test license\n")
        for name in release.NATIVE_FILES:
            if name != "retrolarium-setup.exe":
                (package / name).write_bytes((self.root / name).read_bytes())
        (self.root / "CMakeLists.txt").write_text("project(RetrolariumStudio VERSION 0.2.0)")
        (self.root / "CHANGELOG.md").write_text(CHANGELOG)
        return package

    def test_release_requires_readme_matching_the_package_version(self) -> None:
        package = self.make_package()
        for content in (None, "Retrolarium Studio 9.9.9\n"):
            with self.subTest(content=content):
                path = package / "README.txt"
                if content is None:
                    path.unlink()
                else:
                    path.write_text(content)
                with self.assertRaisesRegex(ValueError, "README|version"):
                    self.release(package)

    def test_release_requires_every_reviewed_qt_notice(self) -> None:
        package = self.make_package()
        notice = release.qt_notices()["components"][0]["notice"]
        (package / "licenses/qt-3rdparty" / notice).unlink()
        with self.assertRaisesRegex(ValueError, "license"):
            self.release(package)

    def test_package_readme_names_the_actual_sources_and_missing_licenses_fail(self) -> None:
        package = self.make_package()
        (package / "BUILD-INFO.txt").write_text("Retrolarium Studio 0.2.0\nKit: " + "3" * 40 + "\n")
        record = json.loads((package / "native-build.json").read_text())
        record["package"] = {"python": {"version": "3.13.1"}, "mame": {"commit": "4" * 40}}
        (package / "native-build.json").write_text(json.dumps(record))
        bios = package / "kit/retrolarium_kit/core/opendisksys/manifest.json"
        bios.parent.mkdir(parents=True, exist_ok=True)
        bios.write_text(json.dumps({"source": "https://github.com/RetrolariumLab/OpenDiskSysROM", "commit": "5" * 40}))
        for name in ("Qt-LGPL-3.0", "OFL-Barlow", "OFL-IBMPlex", "mingw-w64-runtime", "ymfm-BSD-3-Clause"):
            (package / "licenses" / (name + ".txt")).write_text("Synthetic test license\n")
        release.package_notices(package, "0.2.0")
        readme = (package / "README.txt").read_text()
        self.assertIn("retrolarium-kit/tree/" + "3" * 40, readme)
        self.assertIn("retrolarium-core/tree/" + self.state["Core"]["commit"], readme)
        self.assertIn("mamedev/mame/tree/" + "4" * 40, readme)
        (package / "python/LICENSE.txt").unlink()
        with self.assertRaisesRegex(ValueError, "python/LICENSE.txt"):
            release.package_notices(package, "0.2.0")

    def release(self, package: Path) -> Path:
        out = package.parent
        release.write_zip(package, out / "RetrolariumStudio-0.2.0-win64.zip")
        make_setup(out / "RetrolariumStudio-0.2.0-setup.exe", package, "0.2.0")
        with mock.patch.object(release, "STUDIO", self.root):
            release.release_notes(out)
        return out

    def test_release_hashes_describe_the_written_artifacts(self) -> None:
        out = self.release(self.make_package())
        checksums = (out / "SHA256SUMS.txt").read_text()
        self.assertIn(release.digest(out / "RetrolariumStudio-0.2.0-win64.zip"), checksums)
        self.assertIn(release.digest(out / "RetrolariumStudio-0.2.0-setup.exe"), checksums)
        self.assertIn(checksums.splitlines()[0].split()[0], (out / "release-notes.md").read_text())

    def test_release_zip_refuses_roms_and_renamed_local_corpora(self) -> None:
        package = self.make_package()
        for name, content in (
            ("game.nes", b"NES\x1a"),
            ("smb1_ju_music.json", b"{}"),
            ("renamed.json", b'{"provenance":{"kind":"rom-derived"}}'),
        ):
            with self.subTest(name=name):
                path = package / name
                path.write_bytes(content)
                with self.assertRaisesRegex(ValueError, "private game data"):
                    release.write_zip(package, self.root / "must-not-exist.zip")
                self.assertFalse((self.root / "must-not-exist.zip").exists())
                path.unlink()

    def test_release_notes_recheck_an_archive_not_written_by_our_zip_tool(self) -> None:
        package = self.make_package()
        out = self.release(package)
        archive = out / "RetrolariumStudio-0.2.0-win64.zip"
        with zipfile.ZipFile(archive, "a") as bundle:
            bundle.writestr("RetrolariumStudio/renamed.json", '{"provenance":{"kind":"rom-derived"}}')
        with mock.patch.object(release, "STUDIO", self.root), self.assertRaisesRegex(ValueError, "private game data"):
            release.release_notes(out)

    def test_release_notes_come_from_the_changelog(self) -> None:
        notes = (self.release(self.make_package()) / "release-notes.md").read_text()
        self.assertIn("The release's own words.", notes)
        self.assertIn("Kept with it.", notes)
        self.assertNotIn("Not this one.", notes)
        self.assertNotIn("Older.", notes)

    def test_release_needs_a_changelog_section(self) -> None:
        package = self.make_package()
        (self.root / "CHANGELOG.md").write_text("# Changelog\n\n## 0.1.0\n\nOlder.\n")
        with self.assertRaisesRegex(ValueError, "no section for 0.2.0"):
            self.release(package)

    def test_changelog_sections(self) -> None:
        self.assertEqual(release.changelog_section(CHANGELOG, "0.1.0"), "Older.")
        self.assertEqual(release.changelog_section("## [0.2.0] - 2026-11-01\n\nText.\n", "0.2.0"), "Text.")
        with self.assertRaisesRegex(ValueError, "empty section"):
            release.changelog_section("## 0.2.0\n\n## 0.1.0\n\nOlder.\n", "0.2.0")
        with self.assertRaisesRegex(ValueError, "no section"):
            release.changelog_section(CHANGELOG, "0.2")

    def test_release_rejects_a_zip_with_different_native_files(self) -> None:
        (self.root / "CMakeLists.txt").write_text("project(RetrolariumStudio VERSION 0.2.0)")
        (self.root / "CHANGELOG.md").write_text(CHANGELOG)
        archive = self.root / "RetrolariumStudio-0.2.0-win64.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("RetrolariumStudio/BUILD-INFO.txt", "Retrolarium Studio 0.2.0")
            bundle.write(self.root / "native-build.json", "RetrolariumStudio/native-build.json")
            bundle.writestr("RetrolariumStudio/retrolarium-studio.exe", b"old native Studio")
        with mock.patch.object(release, "STUDIO", self.root):
            with self.assertRaisesRegex(ValueError, "different native"):
                release.release_notes(self.root)

    def test_release_rejects_a_zip_with_backslashes(self) -> None:
        package = self.make_package()
        out = package.parent
        archive = out / "RetrolariumStudio-0.2.0-win64.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            for item in sorted(p for p in package.rglob("*") if p.is_file()):
                # ZipInfo normalizes Windows separators in its constructor.
                # Assign the raw filename afterwards to make a genuinely bad ZIP.
                info = zipfile.ZipInfo()
                info.filename = "RetrolariumStudio\\" + item.relative_to(package).as_posix().replace("/", "\\")
                bundle.writestr(info, item.read_bytes())
        make_setup(out / "RetrolariumStudio-0.2.0-setup.exe", package, "0.2.0")
        with mock.patch.object(release, "STUDIO", self.root):
            with self.assertRaisesRegex(ValueError, "backslashes"):
                release.release_notes(out)

    def test_release_rejects_a_setup_with_other_files(self) -> None:
        package = self.make_package()
        out = package.parent
        release.write_zip(package, out / "RetrolariumStudio-0.2.0-win64.zip")
        (package / "tools/rlcore.exe").write_bytes(b"another rlcore")
        make_setup(out / "RetrolariumStudio-0.2.0-setup.exe", package, "0.2.0")
        with mock.patch.object(release, "STUDIO", self.root):
            with self.assertRaisesRegex(ValueError, "different files, first tools/rlcore.exe"):
                release.release_notes(out)

    def test_release_rejects_a_setup_of_another_version(self) -> None:
        package = self.make_package()
        out = package.parent
        release.write_zip(package, out / "RetrolariumStudio-0.2.0-win64.zip")
        make_setup(out / "RetrolariumStudio-0.2.0-setup.exe", package, "0.1.9")
        with mock.patch.object(release, "STUDIO", self.root):
            with self.assertRaisesRegex(ValueError, "version 0.1.9"):
                release.release_notes(out)

    def test_setup_payload_is_checked_file_by_file(self) -> None:
        package = self.make_package()
        setup = self.root / "setup.exe"
        make_setup(setup, package, "0.2.0")
        version, files = release.read_setup_payload(setup)
        self.assertEqual(version, "0.2.0")
        self.assertEqual(files["tools/rlcore.exe"], hashlib.sha256(b"tools/rlcore.exe").hexdigest())
        data = bytearray(setup.read_bytes())
        # The first packed file starts after the stub; flipping a byte of its
        # zlib stream breaks it or its checksum.
        data[len(b"MZ setup stub") + 10] ^= 0xFF
        setup.write_bytes(bytes(data))
        with self.assertRaises((ValueError, zlib.error)):
            release.read_setup_payload(setup)

    def test_setup_without_payload_is_refused(self) -> None:
        setup = self.root / "setup.exe"
        setup.write_bytes(b"MZ just a program" * 4)
        with self.assertRaisesRegex(ValueError, "no Retrolarium Studio payload"):
            release.read_setup_payload(setup)

    def test_zip_is_reproducible_with_forward_slashes(self) -> None:
        package = self.make_package()
        first = self.root / "first.zip"
        second = self.root / "second.zip"
        release.write_zip(package, first)
        (package / "BUILD-INFO.txt").touch()
        release.write_zip(package, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as bundle:
            names = bundle.namelist()
            self.assertEqual(names, sorted(names))
            self.assertIn("RetrolariumStudio/tools/rlcore.exe", names)
            self.assertIn("RetrolariumStudio/mame/roms/fds/", names)
            self.assertFalse(any("\\" in name for name in names))
            self.assertTrue(all(info.date_time == release.ZIP_TIME for info in bundle.infolist()))

    def test_package_record_names_python_and_mame(self) -> None:
        package = self.make_package()
        (package / "python").mkdir(exist_ok=True)
        (package / "python/python.exe").write_bytes(b"python")
        (package / "python/python313.dll").write_bytes(b"runtime")
        (package / "python/python3.dll").write_bytes(b"stable abi")
        (package / "mame/consoles.exe").write_bytes(b"mame")
        (self.root / "tools/mame").mkdir(parents=True)
        (self.root / "tools/mame/COMMIT").write_text("c" * 40 + "\n")
        with mock.patch.object(release, "STUDIO", self.root):
            release.record_package(package, "3.13.1")
            with self.assertRaisesRegex(ValueError, "python312.dll"):
                release.record_package(package, "3.12.8")
        record = json.loads((package / "native-build.json").read_text())
        self.assertEqual(record["package"]["python"]["version"], "3.13.1")
        self.assertEqual(
            record["package"]["python"]["files"]["python/python313.dll"], hashlib.sha256(b"runtime").hexdigest()
        )
        self.assertEqual(record["package"]["mame"]["commit"], "c" * 40)
        self.assertEqual(record["package"]["mame"]["files"]["mame/consoles.exe"], hashlib.sha256(b"mame").hexdigest())
        # release notes check the recorded files against the ZIP.
        out = self.release(package)
        (package / "mame/consoles.exe").write_bytes(b"another mame")
        release.write_zip(package, out / "RetrolariumStudio-0.2.0-win64.zip")
        make_setup(out / "RetrolariumStudio-0.2.0-setup.exe", package, "0.2.0")
        with mock.patch.object(release, "STUDIO", self.root):
            with self.assertRaisesRegex(ValueError, "mame/consoles.exe"):
                release.release_notes(out)

    def test_mame_commit_has_one_source(self) -> None:
        commit = (Path(release.__file__).resolve().parents[1] / "tools/mame/COMMIT").read_text().strip()
        self.assertEqual(release.mame_commit(), commit)
        for script in ("tools/mame/build.sh", "tools/build-mame.sh", "tools/package-win.ps1"):
            text = (Path(release.__file__).resolve().parents[1] / script).read_text(encoding="utf-8")
            self.assertNotIn(commit, text, script)

    def test_build_record_names_the_qt_image(self) -> None:
        release.record_build(self.root, self.source, "sha256:" + "e" * 64)
        record = json.loads((self.root / "native-build.json").read_text())
        self.assertEqual(record["qt_image"], "sha256:" + "e" * 64)
        release.verify_build(self.root)


class CorePinTests(unittest.TestCase):
    def test_mismatch_requires_explicit_development_override(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".github").mkdir()
            (root / ".github/core-ref").write_text("1" * 40 + "\n")
            with mock.patch.object(release, "STUDIO", root), mock.patch.object(release, "git", return_value=b"2" * 40):
                with self.assertRaisesRegex(ValueError, "Core HEAD differs"):
                    release.verify_core_ref()
                release.verify_core_ref(allow_mismatch=True)
            with mock.patch.object(release, "STUDIO", root), mock.patch.object(release, "git", return_value=b"1" * 40):
                release.verify_core_ref()


if __name__ == "__main__":
    unittest.main()
