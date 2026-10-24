"""Scripted interface tests: every ROM in a folder through the studio.

    python tools/autotest/run.py --roms <folder> [--out <folder>] [--studio <folder>]
                                 [--only <text>] [--documents-per-kind 4] [--no-play] [--mame <exe>]

For each ROM the studio runs with --autotest (src/shell/autotest.hpp): it opens
the ROM as a user does (a known game's repository is downloaded and readied),
visits documents of every kind with a screenshot of each, makes a few edits on
each, saves, builds and plays from the selected start. Every run shares one
home of its own (settings, Documents with the downloaded games, MAME states),
so nothing of the user's is touched; each ROM downloads its game afresh.

--studio is a packaged studio (build/package-*/RetrolariumStudio: its kit,
Python and MAME) or a build folder (build/win-amd64), which takes the kit
beside this checkout and Python from PATH; MAME is the package's, else --mame
or RETROLARIUM_MAME. Writes report.html beside the runs.
"""

from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

STUDIO_ROOT = Path(__file__).resolve().parents[2]
EDITORS = ["none", "map", "tiles", "palette", "composition", "table", "sequencer", "emulator", "hex"]


def find_studio(given: Path | None) -> Path:
    if given:
        return given
    packages = sorted((STUDIO_ROOT / "build").glob("package-*/RetrolariumStudio"), key=lambda p: p.stat().st_mtime)
    return packages[-1] if packages else STUDIO_ROOT / "build/win-amd64"


def environment(studio: Path, home: Path, mame: Path | None) -> dict[str, str]:
    env = dict(os.environ)
    env["RETROLARIUM_STUDIO_HOME"] = str(home)
    # The kit's own settings (the remembered MAME) stay the run's too.
    env["RETROLARIUM_SETTINGS"] = str(home / "kit-settings.json")
    if not (studio / "kit/rltool.py").is_file():
        env.setdefault("RETROLARIUM_KIT", str(STUDIO_ROOT.parent / "retrolarium-kit/rltool.py"))
    if mame is not None:
        env["RETROLARIUM_MAME"] = str(mame)
    return env


def run_rom(studio: Path, rom: Path, out: Path, home: Path, args: argparse.Namespace) -> dict:
    output = out / rom.stem
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    scenario = {
        "rom": str(rom),
        "output": str(output),
        "settings": {"openRom/choice": "source"},
        "documents_per_kind": args.documents_per_kind,
        "edits": True,
        "build": True,
        "play": not args.no_play,
    }
    # Each ROM starts from its game as downloaded: earlier runs' edits reach
    # the game's sources too (Flicky's levels), so the games go.
    games = home / "Documents/Retrolarium"
    if games.is_dir():
        shutil.rmtree(games)
    scenario_path = output / "scenario.json"
    scenario_path.write_text(json.dumps(scenario, indent=2), encoding="utf-8")
    started = time.monotonic()
    try:
        process = subprocess.run(
            [str(studio / "retrolarium-studio.exe"), "--autotest", str(scenario_path), "--size", "1600x1000"],
            env=environment(studio, home, args.mame),
            timeout=args.timeout,
            capture_output=True,
        )
        code = process.returncode
    except subprocess.TimeoutExpired:
        code = "timeout"
    results_path = output / "results.json"
    results = json.loads(results_path.read_text(encoding="utf-8")) if results_path.is_file() else {}
    results.setdefault("ok", False)
    results["exit"] = code
    results["wall_seconds"] = round(time.monotonic() - started, 1)
    results["rom_name"] = rom.name
    results["folder"] = output.name
    return results


def edits_summary(document: dict) -> str:
    edits = document.get("edits", [])
    failed = [edit for edit in edits if not edit.get("ok")]
    return f"{len(edits) - len(failed)}/{len(edits)}" if edits else ""


def report(out: Path, runs: list[dict]) -> Path:
    rows = []
    for run in runs:
        folder = run["folder"]
        status = "ok" if run.get("ok") else "FAIL"
        build = run.get("build", {})
        play = run.get("play", {})
        problems = [event["text"] for event in run.get("events", []) if event["kind"] in ("error", "dialog")]
        failed_edits = [
            f"{d['kind']}/{d['document']}: {e['what']}" + (f" — {e['error']}" if e.get("error") else "")
            for d in run.get("documents", [])
            for e in d.get("edits", [])
            if not e.get("ok")
        ]
        errors = [
            m["message"]
            for key in ("open_messages", "save_messages", "messages")
            for m in run.get(key, [])
            if m.get("severity") == "error"
        ] + [
            m["message"]
            for d in run.get("documents", [])
            for m in d.get("messages", [])
            if m.get("severity") == "error"
        ]
        tiles = []
        for document in run.get("documents", []):
            for key in ("view", "edited"):
                if document.get(key):
                    caption = f"{document['kind']}/{document['document']}"
                    if key == "edited":
                        caption += f" · edits {edits_summary(document)}"
                    tiles.append((f"{folder}/{document[key]}", caption))
        for key, caption in (("saved", "saved"),):
            if run.get(key):
                tiles.append((f"{folder}/{run[key]}", caption))
        if build.get("screenshot"):
            tiles.append((f"{folder}/{build['screenshot']}", "build " + ("ok" if build.get("ok") else "FAILED")))
        if play.get("picture"):
            tiles.append((f"{folder}/{play['picture']}", "game " + ("running" if play.get("ok") else "NOT RUNNING")))
        gallery = "".join(
            f'<figure><a href="{html.escape(src)}"><img loading="lazy" src="{html.escape(src)}"></a>'
            f"<figcaption>{html.escape(caption)}</figcaption></figure>"
            for src, caption in tiles
        )
        lists = "".join(
            f"<h4>{title}</h4><ul>" + "".join(f"<li>{html.escape(item)}</li>" for item in items) + "</ul>"
            for title, items in (("Problems", problems), ("Edits refused", failed_edits), ("Errors", errors))
            if items
        )
        rows.append(
            f'<section class="{status.lower()}"><h3>{html.escape(run["rom_name"])} — {status}</h3>'
            f"<p>{len(run.get('documents', []))} documents · build {'ok' if build.get('ok') else 'no'} · "
            f"game {'running' if play.get('ok') else 'no'} · {run.get('wall_seconds')} s · exit {run.get('exit')}</p>"
            f"{lists}<div class=gallery>{gallery}</div></section>"
        )
    passed = sum(1 for run in runs if run.get("ok"))
    page = (
        "<!doctype html><meta charset=utf-8><title>Studio autotest</title><style>"
        "body{font:14px system-ui;background:#16181b;color:#ddd;margin:24px}"
        "section{border:1px solid #333;margin:16px 0;padding:12px}section.fail{border-color:#a33}"
        ".gallery{display:flex;flex-wrap:wrap;gap:8px}figure{margin:0;width:320px}"
        "img{width:320px;border:1px solid #333}figcaption{font-size:12px;color:#aaa}a{color:#8bf}"
        "</style>"
        f"<h1>Studio autotest — {passed}/{len(runs)} passed</h1>" + "".join(rows)
    )
    path = out / "report.html"
    path.write_text(page, encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--roms", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=STUDIO_ROOT / "build/autotest")
    parser.add_argument("--studio", type=Path)
    parser.add_argument("--only", help="run only the ROMs whose names contain this")
    parser.add_argument("--documents-per-kind", type=int, default=4)
    parser.add_argument("--no-play", action="store_true")
    parser.add_argument("--mame", type=Path, help="the MAME a build folder plays with")
    parser.add_argument("--timeout", type=int, default=2400, help="seconds per ROM")
    parser.add_argument("--fresh", action="store_true", help="start from an empty home (downloads every game again)")
    args = parser.parse_args()

    studio = find_studio(args.studio)
    args.out.mkdir(parents=True, exist_ok=True)
    home = args.out / "home"
    if args.fresh and home.exists():
        shutil.rmtree(home)
    home.mkdir(exist_ok=True)
    roms = sorted(
        p for p in args.roms.rglob("*") if p.is_file() and p.suffix.lower() in {".nes", ".fds", ".bin", ".md", ".gen"}
    )
    if args.only:
        roms = [rom for rom in roms if args.only.lower() in rom.name.lower()]
    print(f"studio: {studio}\n{len(roms)} ROM(s)", flush=True)
    runs = []
    for rom in roms:
        result = run_rom(studio, rom, args.out, home, args)
        runs.append(result)
        print(f"{'ok  ' if result['ok'] else 'FAIL'} {rom.name} ({result['wall_seconds']} s)", flush=True)
        (args.out / "runs.json").write_text(json.dumps(runs, indent=2), encoding="utf-8")
    path = report(args.out, runs)
    print(f"report: {path}")
    return 0 if all(run["ok"] for run in runs) else 1


if __name__ == "__main__":
    sys.exit(main())
