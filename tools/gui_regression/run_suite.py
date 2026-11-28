"""Run a declared GUI coverage matrix with private ROMs on a local desktop only."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import subprocess
import sys

import run

EXCLUDED_KINDS = frozenset({"demo", "playtest"})


def load_matrix(path: Path) -> tuple[dict, list[tuple[Path, dict]]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "retrolarium.gui-regression-suite" or manifest.get("schema_version") != 1:
        raise ValueError("unsupported GUI suite schema")
    required = manifest["required_kinds"]
    excluded = manifest["excluded_kinds"]
    if set(excluded) - EXCLUDED_KINDS or len(set(excluded)) != len(excluded) or set(required) & EXCLUDED_KINDS:
        raise ValueError("Only Demo and Playtest may be excluded; neither can be a required tab")
    if not required or len(set(required)) != len(required) or set(required) & set(excluded):
        raise ValueError("the required/excluded tab declarations are inconsistent")
    scenarios = []
    identifiers = set()
    for name in manifest["scenarios"]:
        scenario_path = path.parent / name
        scenario = run.load_scenario(scenario_path)
        if scenario["id"] in identifiers:
            raise ValueError(f"duplicate scenario: {scenario['id']}")
        identifiers.add(scenario["id"])
        if scenario["edit"].get("operation") == "inspect":
            raise ValueError("Read-only inventory is not a regression scenario")
        if (scenario["game"], scenario["revision"]) != (manifest["game"], manifest["revision"]):
            raise ValueError("a scenario belongs to another game/revision")
        if scenario["project"]["kind"] not in required:
            raise ValueError("a scenario opens an excluded or undeclared tab")
        if set(scenario["expected_kinds"]) != set(required) | set(excluded):
            raise ValueError("a scenario's available tab declaration differs from the suite")
        scenarios.append((scenario_path, scenario))
    if {scenario["project"]["kind"] for _, scenario in scenarios} != set(required):
        raise ValueError("the suite is missing a required tab scenario")
    if len({(s["input_sha256"], json.dumps(s["source"], sort_keys=True)) for _, s in scenarios}) != 1:
        raise ValueError("the suite requires one pinned input ROM and source")
    return manifest, scenarios


def aggregate_status(rows: list[dict]) -> str:
    statuses = {row["status"] for row in rows}
    if not statuses or statuses - {"PASS", "FAIL", "MISSING_INPUT", "BASELINE_PENDING"} or "FAIL" in statuses:
        return "FAIL"
    if "MISSING_INPUT" in statuses:
        return "MISSING_INPUT"
    if "BASELINE_PENDING" in statuses:
        return "BASELINE_PENDING"
    return "PASS"


def repeat_hashes_match(rows: list[dict]) -> bool:
    hashes = {}
    for row in rows:
        if row["status"] in ("PASS", "BASELINE_PENDING"):
            hashes.setdefault(row["scenario_id"], set()).add(row["actual_sha256"])
    return all(len(values) == 1 and None not in values for values in hashes.values())


def write_summary(folder: Path, summary: dict) -> None:
    (folder / "suite.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    def escape(value):
        return html.escape(str(value))

    rows = []
    for item in summary["runs"]:
        link = Path(item["report"]).as_uri() if item.get("report") else ""
        rows.append(
            f"<tr><td>{escape(item['scenario_id'])}</td><td>{item['repeat']}</td>"
            f"<td>{escape(item['kind'])}</td><td><a href='{escape(link)}'>{escape(item['status'])}</a></td>"
            f"<td><code>{escape(item.get('actual_sha256') or '')}</code></td>"
            f"<td>{escape(item.get('stop_reason', ''))}</td></tr>"
        )
    document = (
        "<!doctype html><meta charset='utf-8'><title>Local GUI regression suite</title>"
        "<style>body{font:15px system-ui;margin:24px;background:#202326;color:#eee}"
        "table{border-collapse:collapse}td,th{border:1px solid #666;padding:8px}a{color:#9cd2ff}</style>"
        f"<h1>{escape(summary['suite'])}: {escape(summary['status'])}</h1>"
        f"<p>Full declared coverage: {summary['full_coverage']}. "
        f"Required tabs: {escape(', '.join(summary['required_kinds']))}. "
        f"Excluded: {escape(', '.join(summary['excluded_kinds']))}.</p>"
        "<p>Private ROMs run locally. No baseline is approved automatically.</p>"
        "<table><tr><th>Scenario</th><th>Repeat</th><th>Tab</th><th>Result</th><th>SHA-256</th><th>Reason</th></tr>"
        + "".join(rows)
        + "</table>"
    )
    (folder / "suite.html").write_text(document, encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("smb1_ju_suite.json"))
    parser.add_argument("--roms", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--studio", type=Path, required=True)
    parser.add_argument("--kit", type=Path)
    parser.add_argument("--source-archive", type=Path)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument(
        "--stop-on-fail",
        action="store_true",
        help="Stop at the first failed or missing-input run; coverage stays incomplete",
    )
    parser.add_argument("--case", action="append", default=[], help="Select scenario IDs; partial coverage is reported")
    parser.add_argument("--timeout", type=int, default=1800, help="Seconds per independent GUI run")
    args = parser.parse_args(argv)
    if args.repeat < 1 or args.timeout < 1:
        parser.error("--repeat and --timeout must be positive")
    try:
        manifest, matrix = load_matrix(args.manifest.resolve())
        available = {scenario["id"] for _, scenario in matrix}
        if set(args.case) - available:
            raise ValueError("--case contains an unknown scenario ID")
    except (OSError, KeyError, TypeError, ValueError) as error:
        parser.error(str(error))
    output = run.new_run_directory(args.out, manifest["id"])
    full_matrix = not args.case or set(args.case) == available
    planned = (len(set(args.case)) if args.case else len(matrix)) * args.repeat
    summary = dict(
        suite=manifest["id"],
        status="FAIL",
        complete=False,
        planned_runs=planned,
        full_coverage=False,
        required_kinds=manifest["required_kinds"],
        excluded_kinds=manifest["excluded_kinds"],
        runs=[],
    )
    for case_index, (path, scenario) in enumerate(matrix, 1):
        if args.case and scenario["id"] not in args.case:
            continue
        for repeat in range(1, args.repeat + 1):
            folder = output / f"c{case_index:02}-r{repeat:02}"
            folder.mkdir(parents=True)
            command = [
                sys.executable,
                "-B",
                str(Path(__file__).with_name("run.py")),
                "--scenario",
                str(path),
                "--roms",
                str(args.roms.resolve()),
                "--out",
                str(folder),
                "--studio",
                str(args.studio.resolve()),
                "--timeout",
                str(args.timeout),
            ]
            for option in ("kit", "source_archive"):
                value = getattr(args, option)
                if value:
                    command.extend(["--" + option.replace("_", "-"), str(value.resolve())])
            with (folder / "runner.log").open("w", encoding="utf-8") as log:
                process = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
            reports = list(folder.glob("*/report.json"))
            result = json.loads(reports[0].read_text(encoding="utf-8")) if len(reports) == 1 else {}
            status = result.get("status", "FAIL")
            if run.STATUS_EXIT.get(status) != process.returncode:
                status = "FAIL"
                result["stop_reason"] = "The child runner exit code/report disagree or its report is missing"
            item = dict(
                scenario_id=scenario["id"],
                kind=scenario["project"]["kind"],
                repeat=repeat,
                status=status,
                expected_sha256=scenario["expected_sha256"],
                actual_sha256=result.get("actual_sha256"),
                stop_reason=result.get("stop_reason", ""),
                report=str(reports[0].with_suffix(".html")) if reports else None,
            )
            summary["runs"].append(item)
            summary["complete"] = len(summary["runs"]) == planned
            summary["full_coverage"] = (
                full_matrix
                and summary["complete"]
                and aggregate_status(summary["runs"]) in ("PASS", "BASELINE_PENDING")
            )
            summary["repeat_hashes_match"] = repeat_hashes_match(summary["runs"])
            if summary["complete"] and summary["repeat_hashes_match"]:
                summary["status"] = aggregate_status(summary["runs"])
            write_summary(output, summary)
            print(f"{scenario['id']} [{repeat}/{args.repeat}]: {status}", flush=True)
            if args.stop_on_fail and status in ("FAIL", "MISSING_INPUT"):
                summary["status"] = aggregate_status(summary["runs"])
                summary["stopped_early"] = not summary["complete"]
                write_summary(output, summary)
                print(f"report: {output / 'suite.html'}")
                return run.STATUS_EXIT[summary["status"]]
    print(f"report: {output / 'suite.html'}")
    return run.STATUS_EXIT[summary["status"]]


if __name__ == "__main__":
    sys.exit(main())
