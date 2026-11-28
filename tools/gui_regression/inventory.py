"""Audit the complete game/revision matrix against local inputs and GUI evidence.

No GUI or security tests are launched. Private paths appear only in the selected
local report directory. INSPECTED and BASELINE_PENDING never count as coverage.
"""

from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path
import sys

import run
import run_suite


def load_coverage(path: Path, catalog: dict) -> dict:
    matrix = json.loads(path.read_text(encoding="utf-8"))
    if matrix.get("schema") != "retrolarium.gui-coverage" or matrix.get("schema_version") != 1:
        raise ValueError("unsupported GUI coverage schema")
    excluded = matrix.get("excluded_kinds", [])
    if set(excluded) != run_suite.EXCLUDED_KINDS or len(excluded) != len(run_suite.EXCLUDED_KINDS):
        raise ValueError("Only Demo and Playtest may be excluded from whole-catalog coverage")
    declared = [(row["game"], row["revision"]) for row in matrix["revisions"]]
    supported = {(game, rev["id"]): rev for game, entry in catalog["games"].items() for rev in entry["revisions"]}
    if len(declared) != len(set(declared)) or set(declared) != set(supported):
        raise ValueError("coverage must declare every catalog game/revision exactly once")
    for row in matrix["revisions"]:
        if row["input_sha256"] != supported[row["game"], row["revision"]]["sha256"]:
            raise ValueError("coverage input hash differs from the current catalog")
        kinds = row["required_kinds"]
        if not kinds or len(kinds) != len(set(kinds)) or set(kinds) & set(matrix["excluded_kinds"]):
            raise ValueError("coverage has inconsistent required/excluded tabs")
        if row["suite"] is not None:
            manifest, scenarios = run_suite.load_matrix(path.parent / row["suite"])
            if (manifest["game"], manifest["revision"]) != (row["game"], row["revision"]):
                raise ValueError("a suite belongs to a different coverage row")
            if set(manifest["required_kinds"]) != set(kinds):
                raise ValueError("the suite does not cover the declared tab set")
            source = catalog["games"][row["game"]]
            for _, scenario in scenarios:
                if scenario["input_sha256"] != row["input_sha256"]:
                    raise ValueError("a scenario uses a different input revision")
                if any(scenario["source"][key] != source[key] for key in ("repository", "commit", "content_sha256")):
                    raise ValueError("a scenario source pin differs from the catalog")
    return matrix


def input_matches(folder: Path) -> dict[str, list[str]]:
    result = {}
    for path in sorted(folder.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        result.setdefault(run.sha256_file(path), []).append(str(path.resolve()))
    return result


def evidence_reports(roots: list[Path]) -> list[tuple[Path, dict]]:
    paths = set()
    for root in roots:
        if root.is_file():
            paths.add(root.resolve())
        else:
            paths.update(path.resolve() for path in root.glob("*/c??-r??/*/report.json"))
    results = []
    for path in sorted(paths):
        results.append((path, json.loads(path.read_text(encoding="utf-8"))))
    return results


def clean_passes(scenario: dict, reports: list[tuple[Path, dict]]) -> list[str]:
    expected = scenario["expected_sha256"]
    if expected is None:
        return []
    evidence = {}
    for path, report in reports:
        if report.get("status") != "PASS" or report.get("scenario_id") != scenario["id"]:
            continue
        if report.get("input_sha256") != scenario["input_sha256"] or report.get("actual_sha256") != expected:
            continue
        if scenario.get("verify_undo_redo", True) and not report.get("undo_redo_verified"):
            continue
        mirrors = set(scenario.get("mirrored_documents", []))
        if mirrors != set(report.get("mirrored_documents_verified", [])):
            continue
        if not run.presentation_verification_complete(report):
            continue
        recorded = report.get("scenario", {})
        if any(
            recorded.get(key) != scenario.get(key)
            for key in (
                "project",
                "edit",
                "source",
                "expected_kinds",
                "switch_documents",
                "artifact",
                "artifact_size",
                "mirrored_documents",
            )
        ):
            continue
        verification = report.get("game_source", {}).get("verification", {})
        if any(verification.get(key) != scenario["source"].get(key) for key in ("commit", "content_sha256")):
            continue
        artifact = Path(report.get("artifact_path", ""))
        if not artifact.is_file() or run.sha256_file(artifact) != expected:
            continue
        project = report.get("project_path")
        if project:
            evidence[os.path.normcase(str(Path(project).resolve()))] = str(path)
    return list(evidence.values())


def failed_attempts(identifier: str, reports: list[tuple[Path, dict]]) -> list[dict]:
    """Keep historical failures visible, including earlier recipes of this ID."""
    return [
        dict(report=str(path), reason=str(report.get("stop_reason") or ""))
        for path, report in reports
        if report.get("scenario_id") == identifier and report.get("status") == "FAIL"
    ]


def audit(matrix_path: Path, matrix: dict, roms: dict, reports: list[tuple[Path, dict]]) -> dict:
    revisions = []
    for row in matrix["revisions"]:
        matches = roms.get(row["input_sha256"], [])
        scenarios = []
        if row["suite"]:
            _, cases = run_suite.load_matrix(matrix_path.parent / row["suite"])
            for _, case in cases:
                evidence = clean_passes(case, reports)
                scenarios.append(
                    dict(
                        id=case["id"],
                        kind=case["project"]["kind"],
                        baseline_pinned=case["expected_sha256"] is not None,
                        clean_passes=len(evidence),
                        reports=evidence,
                        failed_attempts=failed_attempts(case["id"], reports),
                    )
                )
        if len(matches) != 1:
            status = "MISSING_INPUT" if not matches else "AMBIGUOUS_INPUT"
        elif not scenarios:
            status = "NOT_IMPLEMENTED"
        elif not all(case["baseline_pinned"] for case in scenarios):
            status = "BASELINE_PENDING"
        elif not all(case["clean_passes"] >= 3 for case in scenarios):
            status = "RUNS_REQUIRED"
        else:
            status = "COMPLETE"
        revisions.append(dict(**row, status=status, rom_matches=matches, scenarios=scenarios))
    return dict(
        status="COMPLETE" if all(row["status"] == "COMPLETE" for row in revisions) else "INCOMPLETE",
        games=len({row["game"] for row in revisions}),
        total_revisions=len(revisions),
        complete_revisions=sum(row["status"] == "COMPLETE" for row in revisions),
        historical_failed_attempts=sum(len(case["failed_attempts"]) for row in revisions for case in row["scenarios"]),
        revisions=revisions,
    )


def write_reports(folder: Path, report: dict) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "coverage.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    rows = []
    for row in report["revisions"]:
        cases = row["scenarios"]
        passed = sum(case["clean_passes"] >= 3 for case in cases)
        rows.append(
            "<tr>"
            + "".join(
                f"<td>{html.escape(str(value))}</td>"
                for value in (
                    row["game"],
                    row["revision"],
                    row["status"],
                    len(row["rom_matches"]),
                    ", ".join(row["required_kinds"]),
                    f"{passed}/{len(cases)}",
                    sum(len(case["failed_attempts"]) for case in cases),
                )
            )
            + "</tr>"
        )
    failures = []
    for row in report["revisions"]:
        for case in row["scenarios"]:
            for failure in case["failed_attempts"]:
                path = Path(failure["report"])
                page = path.with_suffix(".html")
                link = (page if page.is_file() else path).resolve().as_uri()
                failures.append(
                    f'<li><a href="{html.escape(link)}">{html.escape(case["id"])}</a>: '
                    f"{html.escape(failure['reason'])}</li>"
                )
    document = (
        "<!doctype html><meta charset='utf-8'><title>GUI revision coverage</title>"
        "<style>body{font:15px system-ui;margin:24px}td,th{border:1px solid #aaa;padding:8px}table{border-collapse:collapse}</style>"
        f"<h1>{report['status']}: {report['complete_revisions']}/{report['total_revisions']} revisions</h1>"
        "<p>COMPLETE requires a pinned baseline and three independent local PASS builds for every scenario. "
        "Read-only GUI inspection does not count. Demo and Playtest are excluded.</p>"
        "<table><tr><th>Game</th><th>Revision</th><th>Status</th><th>Exact ROM matches</th><th>Required tabs</th>"
        "<th>Scenarios with three clean passes</th><th>Historical FAIL records</th></tr>" + "".join(rows) + "</table>"
        f"<p>Historical failed attempts: {report['historical_failed_attempts']}. "
        "These remain FAIL and never count as PASS evidence. The history includes earlier recipes.</p>"
        "<details><summary>Historical failed-attempt reports</summary><ul>" + "".join(failures) + "</ul></details>"
    )
    (folder / "coverage.html").write_text(document, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, default=Path(__file__).with_name("coverage.json"))
    parser.add_argument("--kit", type=Path, default=run.KIT_ROOT)
    parser.add_argument("--roms", type=Path, required=True)
    parser.add_argument("--reports", type=Path, action="append", default=[])
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        catalog = json.loads(
            (args.kit / "src/retrolarium_kit/core/profiles/repositories.json").read_text(encoding="utf-8")
        )
        matrix = load_coverage(args.matrix, catalog)
        if not args.roms.is_dir():
            raise ValueError("--roms must name the owner's local ROM directory")
        report = audit(args.matrix, matrix, input_matches(args.roms), evidence_reports(args.reports))
        write_reports(args.out, report)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(f"{report['status']}: {report['complete_revisions']}/{report['total_revisions']} revisions")
    print(f"report: {args.out.resolve() / 'coverage.html'}")
    if any(row["status"] in ("MISSING_INPUT", "AMBIGUOUS_INPUT") for row in report["revisions"]):
        return 2
    return 0 if report["status"] == "COMPLETE" else 3


if __name__ == "__main__":
    sys.exit(main())
