"""Run the four full SMB1 JU comparisons on private, locally generated corpora."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

CASES = {
    "smb_level_renderer_golden_test": "levels.json",
    "smb_area_streams_test": "levels.json",
    "smb_screens_test": "screens.json",
    "smb_music_test": "music.json",
}


def validate_corpus(corpus: Path) -> None:
    hashes = set()
    for name in sorted(set(CASES.values())):
        root = json.loads((corpus / name).read_text(encoding="utf-8"))
        provenance = root.get("provenance", {})
        sha = provenance.get("rom_sha256", "")
        if provenance.get("kind") != "rom-derived" or not re.fullmatch(r"[0-9a-f]{64}", sha):
            raise ValueError(f"{name}: local integration requires ROM-derived data, not public synthetic fixtures")
        hashes.add(sha)
        if name == "levels.json" and root.get("profile") != "smb1.ju":
            raise ValueError("levels.json: expected the SMB1 JU profile")
    if len(hashes) != 1:
        raise ValueError("corpora were generated from different ROMs")


def run_suite(binary: Path, corpus: Path, environment: dict[str, str], timeout: float) -> int:
    validate_corpus(corpus)
    programs = {name: binary / (name + (".exe" if os.name == "nt" else "")) for name in CASES}
    for program in programs.values():
        if not program.is_file():
            raise ValueError(f"missing test program: {program}")
    failed = 0
    for name, fixture in CASES.items():
        print(f"== {name} (local ROM integration)", flush=True)
        try:
            result = subprocess.run([str(programs[name]), str(corpus / fixture)], env=environment, timeout=timeout)
            if result.returncode != 0:  # A local integration skip is a failure too.
                failed += 1
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"FAIL: {error}", flush=True)
            failed += 1
    print(f"SMB ROM integration: {len(CASES) - failed} passed, {failed} failed", flush=True)
    return int(failed != 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True, help="installed native Studio test binaries")
    parser.add_argument("--corpus", type=Path, required=True, help="Kit's ignored build/smb-rom-corpus directory")
    parser.add_argument("--qt-prefix", type=Path, help="host Qt SDK for shared Windows builds")
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    environment = dict(os.environ)
    if args.qt_prefix:
        environment["PATH"] = str(args.qt_prefix.resolve() / "bin") + os.pathsep + environment.get("PATH", "")
    try:
        return run_suite(args.bin.resolve(), args.corpus.resolve(), environment, args.timeout)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
