"""Record native build provenance and generate release notes from packaged files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

STUDIO = Path(__file__).resolve().parents[1]
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


def record_build(bin_dir: Path, source_record: Path) -> None:
    state = json.loads(source_record.read_text(encoding="utf-8"))
    if state != sources():
        raise ValueError("Studio or Core changed during the native build; rebuild before packaging")
    files = {name: digest(bin_dir / name) for name in NATIVE_FILES}
    write(bin_dir / "native-build.json", {"schema_version": 1, "sources": state, "files": files})


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
    name = f"RetrolariumStudio-{version}"
    archive = out / f"{name}-win64.zip"
    setup = out / f"{name}-setup.exe"
    with zipfile.ZipFile(archive) as bundle:
        info = bundle.read("RetrolariumStudio/BUILD-INFO.txt").decode("utf-8-sig")
        record = json.loads(bundle.read("RetrolariumStudio/native-build.json"))
        if record["sources"] != sources() or any(v["changes"] for v in record["sources"].values()):
            raise ValueError("release archive is from different or uncommitted native sources")
        for name in NATIVE_FILES:
            if name == "retrolarium-setup.exe":
                continue
            executable = bundle.read("RetrolariumStudio/" + name)
            if hashlib.sha256(executable).hexdigest() != record["files"][name]:
                raise ValueError(f"release archive contains a different native executable: {name}")
        if not info.startswith(f"Retrolarium Studio {version}"):
            raise ValueError("release archive has a different version")
    entries = [(setup.name, digest(setup)), (archive.name, digest(archive))]
    (out / "SHA256SUMS.txt").write_text("".join(f"{sha}  {filename}\n" for filename, sha in entries), encoding="ascii")
    text = f"""# Retrolarium Studio {version}

This release adds Alien Soldier (Japan and Europe) to the native Studio,
alongside Super Mario Bros., Castle Excellent / Castlequest, Solomon's Key,
Pac-Man, Doraemon and Flicky.

Alien Soldier offers stage maps and objects, tiles, metatiles, palettes,
sprite mappings and animations, screens, text, driver-based sound previews,
and its four attract demos. Edits retain each payload's original capacity.
The source acceptance baseline covers both unchanged ROM builds and the
Japanese attract sequence on a relocated build. Full-game replay and European
runtime acceptance require further checks.

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
    verify = sub.add_parser("verify-build")
    verify.add_argument("bin", type=Path)
    verify.add_argument("--allow-dirty", action="store_true")
    notes = sub.add_parser("notes")
    notes.add_argument("out", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "record-source":
            write(args.output, sources())
        elif args.command == "record-build":
            record_build(args.bin, args.source_record)
        elif args.command == "verify-build":
            verify_build(args.bin, args.allow_dirty)
        else:
            release_notes(args.out)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"{exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
