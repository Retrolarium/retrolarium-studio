"""Capture one revision's visible tabs/documents without claiming a regression PASS."""

import argparse
import json
from pathlib import Path
import sys
import uuid

import run


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--roms", type=Path, required=True)
    parser.add_argument("--studio", type=Path, required=True)
    parser.add_argument("--kit", type=Path, default=run.KIT_ROOT)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--source-archive", type=Path)
    parser.add_argument("--document", action="append", default=[], metavar="KIND=ID")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args(argv)
    kit = run.effective_kit(args.studio.resolve(), args.kit)
    catalog = json.loads((kit / "src/retrolarium_kit/core/profiles/repositories.json").read_text(encoding="utf-8"))
    source = catalog["games"].get(args.game)
    if source is None:
        parser.error("the game is absent from the selected Kit catalog")
    revision = next((item for item in source["revisions"] if item["id"] == args.revision), None)
    if revision is None:
        parser.error("the revision is absent from the selected game's catalog entry")
    documents = {}
    for choice in args.document:
        kind, separator, identifier = choice.partition("=")
        if not separator or not identifier or kind in ("demo", "playtest"):
            parser.error("--document needs KIND=ID for an included tab")
        documents.setdefault(kind, []).append(identifier)
    scenario = dict(
        schema="retrolarium.gui-regression",
        schema_version=1,
        id=f"inspect-{args.game}-{args.revision}",
        game=args.game,
        revision=args.revision,
        revision_name=revision["name"],
        input_sha256=revision["sha256"],
        expected_sha256=None,
        source={key: source[key] for key in ("repository", "descriptor", "ref", "commit", "content_sha256")},
        project={},
        edit={"operation": "inspect"},
        artifact="inspection-only",
        expected_kinds=[],
        inspect_documents=documents,
        timeouts={"open_s": 900, "editor_s": 120},
    )
    folder = args.out.resolve() / "inspection-inputs"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{scenario['id']}-{uuid.uuid4().hex[:8]}.json"
    path.write_text(json.dumps(scenario, indent=2) + "\n", encoding="utf-8")
    command = [
        "--inspect",
        "--scenario",
        str(path),
        "--roms",
        str(args.roms),
        "--studio",
        str(args.studio),
        "--kit",
        str(kit),
        "--out",
        str(args.out),
        "--timeout",
        str(args.timeout),
    ]
    if args.source_archive:
        command.extend(["--source-archive", str(args.source_archive)])
    return run.main(command)


if __name__ == "__main__":
    sys.exit(main())
