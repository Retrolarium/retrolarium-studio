"""Record native build provenance, zip the package and generate release notes from packaged files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zipfile
import zlib

STUDIO = Path(__file__).resolve().parents[1]
# The setup program's payload (installer/src/installer_package.cpp): the
# installer, the zlib-compressed files (qCompress: a big-endian size, then a
# zlib stream), a JSON manifest and a footer that locates the manifest.
PAYLOAD_MAGIC = b"RETROLARIUM-PKG1"
PAYLOAD_FOOTER = 32
PAYLOAD_FORMAT = 1
# One fixed time for every ZIP entry: the same package gives the same ZIP.
ZIP_TIME = (1980, 1, 1, 0, 0, 0)
NATIVE_FILES = (
    "retrolarium-studio.exe",
    "retrolarium-setup.exe",
    "tools/retrolarium-flicky-level-writer.exe",
    "tools/retrolarium-ymfm-renderer.exe",
    "tools/rlcore.exe",
)


def git(root: Path, *args: str) -> bytes:
    return subprocess.run(
        ["git", "--no-optional-locks", "-C", str(root), *args],
        env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"},
        check=True,
        capture_output=True,
    ).stdout


def sources() -> dict:
    result = {}
    for name, root in (("Studio", STUDIO), ("Core", STUDIO.parent / "retrolarium-core")):
        diff = git(root, "diff", "--binary", "HEAD")
        untracked = git(root, "ls-files", "--others", "--exclude-standard", "-z")
        changes = hashlib.sha256(diff)
        for entry in untracked.split(b"\0"):
            if entry:
                changes.update(entry)
                changes.update((root / os.fsdecode(entry)).read_bytes())
        result[name] = {
            "commit": git(root, "rev-parse", "HEAD").decode().strip(),
            "changes": changes.hexdigest() if diff or untracked else None,
        }
    return result


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def record_build(bin_dir: Path, source_record: Path, qt_image: str | None = None) -> None:
    state = json.loads(source_record.read_text(encoding="utf-8"))
    if state != sources():
        raise ValueError("Studio or Core changed during the native build; rebuild before packaging")
    files = {name: digest(bin_dir / name) for name in NATIVE_FILES}
    record = {"schema_version": 1, "sources": state, "files": files}
    if qt_image:
        # The Qt SDK image the build ran in (its image ID: a local build has
        # no registry digest).
        record["qt_image"] = qt_image
    write(bin_dir / "native-build.json", record)


def mame_commit() -> str:
    """The MAME commit the package ships: tools/mame/COMMIT, which the MAME builds read too."""
    commit = (STUDIO / "tools/mame/COMMIT").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError(f"tools/mame/COMMIT does not hold a commit: {commit!r}")
    return commit


def record_package(package: Path, python_version: str) -> None:
    """Adds what the package carries besides the native build to its copy of
    native-build.json: the private Python's version and hashes, and MAME's
    commit and hash."""
    match = re.fullmatch(r"(\d+)\.(\d+)\.\d+\S*", python_version)
    if not match:
        raise ValueError(f"not a Python version: {python_version!r}")
    runtime = package / "python"
    dll = f"python{match.group(1)}{match.group(2)}.dll"
    if not (runtime / "python.exe").is_file() or not (runtime / dll).is_file():
        raise ValueError(f"the package's Python has no python.exe or {dll} for Python {python_version}")
    python_files = {
        f"python/{path.name}": digest(path)
        for path in sorted([*runtime.glob("python*.exe"), *runtime.glob("python3*.dll")])
    }
    mame = package / "mame/consoles.exe"
    if not mame.is_file():
        raise ValueError("the package has no mame/consoles.exe")
    record_path = package / "native-build.json"
    record = json.loads(record_path.read_text(encoding="utf-8"))
    record["package"] = {
        "python": {"version": python_version, "files": python_files},
        "mame": {"commit": mame_commit(), "files": {"mame/consoles.exe": digest(mame)}},
    }
    write(record_path, record)


def private_game_data(name: str, data: bytes | None = None) -> bool:
    """Keep ROMs and locally generated game corpora out of release artifacts."""
    path = Path(name.replace("\\", "/").lower())
    if path.suffix in {".nes", ".fds", ".rom", ".sfc", ".smc", ".gen", ".mdrom"}:
        return True
    if path.name in {"smb1_ju_level_scenes.json", "smb1_ju_screens.json", "smb1_ju_music.json"}:
        return True
    if "smb-rom-corpus" in path.parts or "tests/retrolarium/" in path.as_posix():
        return True
    if path.suffix == ".json" and data is not None:
        try:
            document = json.loads(data)
        except (ValueError, UnicodeError):
            return False
        if isinstance(document, dict) and isinstance(document.get("provenance"), dict):
            return document["provenance"].get("kind") == "rom-derived"
    return False


def write_zip(package: Path, output: Path) -> None:
    """The package folder as a ZIP under its own name, reproducibly: sorted
    entries, '/' separators and one fixed time. Empty folders are kept."""
    entries = []
    for path in package.rglob("*"):
        relative = path.relative_to(package).as_posix()
        if path.is_file():
            if private_game_data(relative, path.read_bytes() if path.suffix.lower() == ".json" else None):
                raise ValueError(f"private game data cannot enter a release: {relative}")
            entries.append((relative, path))
        elif path.is_dir() and not any(path.iterdir()):
            entries.append((relative + "/", None))
    partial = output.with_name(output.name + ".partial")
    with zipfile.ZipFile(partial, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for relative, path in sorted(entries):
            info = zipfile.ZipInfo(f"{package.name}/{relative}", date_time=ZIP_TIME)
            if path is None:
                info.external_attr = (0o40755 << 16) | 0x10
                bundle.writestr(info, b"")
            else:
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                bundle.writestr(info, path.read_bytes(), compresslevel=9)
    partial.replace(output)


def _payload_end(stream, size: int) -> int:
    """Where the payload ends: before the certificate table of a signed setup."""
    stream.seek(0x3C)
    raw = stream.read(4)
    if len(raw) != 4:
        return size
    pe = int.from_bytes(raw, "little")
    stream.seek(pe)
    header = stream.read(26)
    if len(header) != 26 or not header.startswith(b"PE\0\0"):
        return size
    magic = int.from_bytes(header[24:26], "little")
    stream.seek(pe + 24 + (112 if magic == 0x20B else 96) + 4 * 8)
    security = stream.read(8)
    if len(security) != 8:
        return size
    start, length = struct.unpack("<II", security)
    if start == 0 or length == 0 or start + length != size:
        return size
    return start


def _payload_path(name: object) -> bool:
    return (
        isinstance(name, str)
        and bool(name)
        and "\\" not in name
        and ":" not in name
        and not name.startswith("/")
        and all(part not in ("", ".", "..") for part in name.split("/"))
    )


def read_setup_payload(setup: Path) -> tuple[str, dict[str, str]]:
    """The version and the files (path: SHA-256) of a setup program's payload,
    each unpacked and checked against its manifest hash."""
    damaged = ValueError(f"{setup.name}: the payload manifest is damaged")
    with setup.open("rb") as stream:
        size = stream.seek(0, os.SEEK_END)
        end = _payload_end(stream, size)
        footer = None
        for padding in range(1 if end == size else 8):
            if end - padding < PAYLOAD_FOOTER:
                break
            stream.seek(end - padding - PAYLOAD_FOOTER)
            tail = stream.read(PAYLOAD_FOOTER + padding)
            if tail.startswith(PAYLOAD_MAGIC) and tail[PAYLOAD_FOOTER:].count(0) == padding:
                footer = tail[:PAYLOAD_FOOTER]
                end -= padding
                break
        if footer is None:
            raise ValueError(f"{setup.name} holds no Retrolarium Studio payload")
        manifest_offset, manifest_size = struct.unpack("<QQ", footer[len(PAYLOAD_MAGIC) :])
        if manifest_size == 0 or manifest_offset + manifest_size + PAYLOAD_FOOTER != end:
            raise damaged
        stream.seek(manifest_offset)
        try:
            manifest = json.loads(stream.read(manifest_size))
        except ValueError:
            raise damaged from None
        if not isinstance(manifest, dict) or manifest.get("format") != PAYLOAD_FORMAT:
            raise damaged
        files: dict[str, str] = {}
        for entry in manifest.get("files", []):
            name = entry.get("path")
            if not _payload_path(name) or name in files:
                raise ValueError(f"{setup.name}: invalid payload path {name!r}")
            stream.seek(int(entry["offset"]))
            packed = stream.read(int(entry["packedSize"]))
            if len(packed) != int(entry["packedSize"]) or len(packed) < 4:
                raise ValueError(f"{setup.name}: payload file {name} is truncated")
            if int.from_bytes(packed[:4], "big") != int(entry["size"]):
                raise ValueError(f"{setup.name}: payload file {name} has a wrong size")
            plain = zlib.decompress(packed[4:]) if entry["size"] else b""
            sha = hashlib.sha256(plain).hexdigest()
            if len(plain) != int(entry["size"]) or sha != str(entry["sha256"]).lower():
                raise ValueError(f"{setup.name}: payload file {name} does not match its checksum")
            files[name] = sha
        version = manifest.get("version")
        if not isinstance(version, str) or not files:
            raise damaged
        return version, files


def changelog_section(text: str, version: str) -> str:
    """The body of CHANGELOG.md's "## <version>" section."""
    heading = re.compile(r"^##\s+\[?v?" + re.escape(version) + r"\]?(?:\s.*)?$")
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if heading.match(line):
            body = []
            for following in lines[index + 1 :]:
                if re.match(r"^##?\s", following):
                    break
                body.append(following)
            section = "\n".join(body).strip()
            if not section:
                raise ValueError(f"CHANGELOG.md has an empty section for {version}")
            return section
    raise ValueError(f"CHANGELOG.md has no section for {version}; add one before releasing")


def verify_build(bin_dir: Path, allow_dirty: bool = False) -> None:
    record = json.loads((bin_dir / "native-build.json").read_text(encoding="utf-8"))
    if record.get("schema_version") != 1 or record.get("sources") != sources():
        raise ValueError("native build does not match the current Studio and Core sources; rebuild")
    if not allow_dirty and any(item["changes"] for item in record["sources"].values()):
        raise ValueError("native build contains uncommitted source changes")
    if set(record["files"]) != set(NATIVE_FILES):
        raise ValueError("native build record does not cover all packaged executables")
    for name, expected in record["files"].items():
        if digest(bin_dir / name) != expected:
            raise ValueError(f"native binary changed after the build: {name}")


def release_notes(out: Path) -> None:
    version = re.search(
        r"project\([^)]*VERSION\s+(\d+\.\d+\.\d+)", (STUDIO / "CMakeLists.txt").read_text(), re.S
    ).group(1)
    changes = changelog_section((STUDIO / "CHANGELOG.md").read_text(encoding="utf-8"), version)
    name = f"RetrolariumStudio-{version}"
    archive = out / f"{name}-win64.zip"
    setup = out / f"{name}-setup.exe"
    prefix = "RetrolariumStudio/"
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        # ZipInfo.filename is normalized on Windows; orig_filename retains
        # the separators actually stored in the archive.
        if any("\\" in entry.orig_filename for entry in bundle.infolist()):
            raise ValueError("release archive has file names with backslashes; build it with release.py zip")
        for entry in names:
            if private_game_data(entry, bundle.read(entry) if entry.lower().endswith(".json") else None):
                raise ValueError(f"private game data cannot enter a release: {entry}")
        info = bundle.read(prefix + "BUILD-INFO.txt").decode("utf-8-sig")
        record = json.loads(bundle.read(prefix + "native-build.json"))
        if record["sources"] != sources() or any(v["changes"] for v in record["sources"].values()):
            raise ValueError("release archive is from different or uncommitted native sources")
        zipped = {
            entry[len(prefix) :]: hashlib.sha256(bundle.read(entry)).hexdigest()
            for entry in names
            if entry.startswith(prefix) and not entry.endswith("/")
        }
        for name in NATIVE_FILES:
            if name == "retrolarium-setup.exe":
                continue
            if zipped.get(name) != record["files"][name]:
                raise ValueError(f"release archive contains a different native executable: {name}")
        # What the package carries besides the native build (record-package).
        for part in record.get("package", {}).values():
            for name, expected in part["files"].items():
                if zipped.get(name) != expected:
                    raise ValueError(f"release archive contains a different {name} than its record")
        if not info.startswith(f"Retrolarium Studio {version}"):
            raise ValueError("release archive has a different version")
    payload_version, payload = read_setup_payload(setup)
    if payload_version != version:
        raise ValueError(f"{setup.name} carries version {payload_version}, not {version}")
    if payload != zipped:
        differences = sorted(set(payload) ^ set(zipped)) or sorted(n for n in payload if payload[n] != zipped[n])
        raise ValueError(f"{setup.name} and {archive.name} hold different files, first {differences[0]}")
    entries = [(setup.name, digest(setup)), (archive.name, digest(archive))]
    (out / "SHA256SUMS.txt").write_text("".join(f"{sha}  {filename}\n" for filename, sha in entries), encoding="ascii")
    text = f"""# Retrolarium Studio {version}

{changes}

## Install

Windows 10 or 11, 64-bit. Run the setup for your user, or unpack the ZIP into
a writable folder. Bring your own ROMs. The package carries Kit, Python, MAME
and OpenDiskSysROM. The application is unsigned.

[User guide](https://github.com/Retrolarium/retrolarium-studio/blob/v{version}/docs/user_guide.md)

## SHA-256

| File | SHA-256 |
| --- | --- |
"""
    text += "".join(f"| {filename} | {sha} |\n" for filename, sha in entries)
    text += "\n## Build provenance\n\n~~~text\n" + info.rstrip() + "\n~~~\n"
    (out / "release-notes.md").write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    source = sub.add_parser("record-source")
    source.add_argument("output", type=Path)
    build = sub.add_parser("record-build")
    build.add_argument("bin", type=Path)
    build.add_argument("source_record", type=Path)
    build.add_argument("--qt-image", help="the ID of the Qt SDK image the build ran in")
    verify = sub.add_parser("verify-build")
    verify.add_argument("bin", type=Path)
    verify.add_argument("--allow-dirty", action="store_true")
    package = sub.add_parser("record-package")
    package.add_argument("package", type=Path)
    package.add_argument("--python-version", required=True)
    archive = sub.add_parser("zip")
    archive.add_argument("package", type=Path)
    archive.add_argument("output", type=Path)
    notes = sub.add_parser("notes")
    notes.add_argument("out", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "record-source":
            write(args.output, sources())
        elif args.command == "record-build":
            record_build(args.bin, args.source_record, args.qt_image)
        elif args.command == "verify-build":
            verify_build(args.bin, args.allow_dirty)
        elif args.command == "record-package":
            record_package(args.package, args.python_version)
        elif args.command == "zip":
            write_zip(args.package, args.output)
        else:
            release_notes(args.out)
    except (OSError, ValueError, KeyError, TypeError, zlib.error, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"{exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
