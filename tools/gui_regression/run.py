"""Run one private, real-widget GUI regression in an isolated Studio home.

    python tools/gui_regression/run.py --roms <folder> \
        --scenario tools/gui_regression/smb1_ju_block.json \
        --out build/gui-regression --studio build/win-amd64

The ROM is selected only by its complete SHA-256. Nothing is downloaded by this
runner and no disks are searched beyond --roms. Studio itself prepares the
pinned game repository through the real GUI. Every invocation gets a new run
folder; the runner never removes earlier runs or touches another Studio.

Exit codes: 0 PASS, 1 FAIL, 2 MISSING_INPUT, 3 BASELINE_PENDING, 4 INSPECTED.
Inspection captures read-only GUI metadata, never a regression PASS.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import html
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import uuid

STUDIO_ROOT = Path(__file__).resolve().parents[2]
KIT_ROOT = STUDIO_ROOT.parent / "retrolarium-kit"
CORE_ROOT = STUDIO_ROOT.parent / "retrolarium-core"
STATUS_EXIT = {"PASS": 0, "FAIL": 1, "MISSING_INPUT": 2, "BASELINE_PENDING": 3, "INSPECTED": 4}
SHA256 = re.compile(r"[0-9a-f]{64}")


class InputError(RuntimeError):
    """The one exact private input ROM is absent or ambiguous."""


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_scenario(path: Path) -> dict:
    scenario = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(scenario, dict):
        raise ValueError("the scenario must be a JSON object")
    if scenario.get("schema") != "retrolarium.gui-regression" or scenario.get("schema_version") != 1:
        raise ValueError("unsupported GUI regression scenario schema")
    identifier = scenario.get("id")
    if not isinstance(identifier, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]*", identifier):
        raise ValueError("the scenario needs a filesystem-safe id")
    digest = scenario.get("input_sha256")
    if not isinstance(digest, str) or not SHA256.fullmatch(digest):
        raise ValueError("input_sha256 must be exactly 64 lowercase hexadecimal digits")
    expected = scenario.get("expected_sha256")
    if expected is not None and (not isinstance(expected, str) or not SHA256.fullmatch(expected)):
        raise ValueError("expected_sha256 must be null or exactly 64 lowercase hexadecimal digits")
    size = scenario.get("artifact_size")
    if size is not None and (type(size) is not int or size <= 0):
        raise ValueError("artifact_size must be a positive integer when declared")
    mirrors = scenario.get("mirrored_documents", [])
    if not isinstance(mirrors, list) or not all(isinstance(item, str) for item in mirrors):
        raise ValueError("mirrored_documents must be a list of fixed document IDs")
    if len(set(mirrors)) != len(mirrors) or not set(mirrors) <= set(scenario.get("switch_documents", [])):
        raise ValueError("every shared document must be visited by the declared navigation")
    for key in ("revision_name", "source", "project", "edit", "artifact", "timeouts"):
        if key not in scenario:
            raise ValueError(f"the scenario has no {key}")
    edit = scenario["edit"]
    if not isinstance(edit, dict):
        raise ValueError("edit must be an object with a fixed GUI operation")
    library_item = edit.get("library_item")
    if library_item is not None and (not isinstance(library_item, str) or not library_item):
        raise ValueError("library_item must be a nonempty fixed card ID")
    positions = edit.get("object_positions", [])
    if not isinstance(positions, list):
        raise ValueError("object_positions must list explicit map-object translations")
    moved = set()
    for position in positions:
        if not isinstance(position, dict) or not isinstance(position.get("id"), str) or not position["id"]:
            raise ValueError("each object translation needs a fixed ID")
        if position["id"] in moved:
            raise ValueError("object translation IDs must be unique")
        moved.add(position["id"])
        if any(type(position.get(key)) is not int for key in ("dx", "dy")):
            raise ValueError("object translations need integer cell deltas")
        if position["dx"] == position["dy"] == 0:
            raise ValueError("an object translation must change its position")
    changes = edit.get("state_changes", [])
    if not isinstance(changes, list):
        raise ValueError("state_changes must list fixed coupled snapshot changes")
    addresses = set()
    for change in changes:
        if not isinstance(change, dict):
            raise ValueError("each state change must be an object")
        address = change.get("path")
        if not isinstance(address, list) or not address or not all(isinstance(part, str) and part for part in address):
            raise ValueError("a coupled state change needs a nonempty string path")
        address = tuple(address)
        if address in addresses:
            raise ValueError("coupled state change paths must be unique")
        addresses.add(address)
        if "old" not in change or "new" not in change or change["old"] == change["new"]:
            raise ValueError("coupled state changes must pin distinct old and new values")
    return scenario


def find_rom(roms: Path, expected_sha256: str) -> Path:
    """Find one content match below roms, ignoring names and extensions."""
    root = roms.resolve()
    if not root.is_dir():
        raise InputError(f"The ROM folder does not exist: {root}")
    matches = []
    unreadable = []
    for path in sorted(root.rglob("*")):
        # A link can point outside the folder the owner explicitly supplied.
        if path.is_symlink() or not path.is_file():
            continue
        try:
            path.resolve().relative_to(root)
            digest = sha256_file(path)
        except OSError as error:
            unreadable.append(f"{path}: {error}")
            continue
        if digest == expected_sha256:
            matches.append(path)
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        locations = ", ".join(str(path) for path in matches)
        raise InputError(
            f"The required ROM SHA-256 {expected_sha256} is ambiguous; "
            f"{len(matches)} files have exactly that content: {locations}"
        )
    detail = f" Unreadable files: {'; '.join(unreadable)}" if unreadable else ""
    raise InputError(f"No file below {root} has the required ROM SHA-256 {expected_sha256}.{detail}")


def new_run_directory(out: Path, identifier: str) -> Path:
    """Reserve a unique directory without deleting or reusing an earlier run."""
    # Both a suite and its case reserve a directory. Leave room for the
    # deepest native assembler include, not just the source working directory.
    # Full identities and UTC timestamps remain in reports rather than being
    # repeated in every component of a Windows build path.
    label = identifier[:8].rstrip("._-") or "run"
    path = out.resolve() / f"{label}-{uuid.uuid4().hex[:12]}"
    path.mkdir(parents=True, exist_ok=False)
    return path


def packaged(studio: Path) -> bool:
    return (studio / "kit/rltool.py").is_file()


def effective_kit(studio: Path, selected_kit: Path) -> Path:
    """Use exactly the Kit that Studio will use for this invocation."""
    return (studio / "kit" if packaged(studio) else selected_kit).resolve()


def studio_executable(studio: Path) -> Path:
    name = "retrolarium-gui-regression.exe" if os.name == "nt" else "retrolarium-gui-regression"
    primary = studio / name
    candidates = [primary, studio / ".cmake-build" / name]
    for configuration in ("Release", "RelWithDebInfo", "MinSizeRel", "Debug"):
        candidates.extend(
            (
                studio / configuration / name,
                studio / ".cmake-build" / configuration / name,
            )
        )
    return next((path for path in candidates if path.is_file()), primary)


def build_core_executable(studio: Path) -> Path | None:
    """Find only the Core CLI belonging to the selected CMake Studio build."""
    name = "rlcore.exe" if os.name == "nt" else "rlcore"
    configurations = ("Release", "RelWithDebInfo", "MinSizeRel", "Debug")
    roots = [studio, studio / ".cmake-build"]
    if studio.name in configurations:
        roots.append(studio.parent)
    for root in roots:
        for folder in (root / "retrolarium_core", *(root / "retrolarium_core" / item for item in configurations)):
            candidate = folder / name
            if candidate.is_file():
                return candidate.resolve()
    return None


def environment(
    studio: Path,
    home: Path,
    scratch: Path,
    kit: Path | None = None,
    source_archive: Path | None = None,
) -> dict[str, str]:
    """Isolate Studio, Kit, settings, caches and temporary compiler files."""
    env = dict(os.environ)
    home = home.resolve()
    scratch = scratch.resolve()
    values = {
        "RETROLARIUM_STUDIO_HOME": home,
        "RETROLARIUM_SETTINGS": home / "kit-settings.json",
        "RETROLARIUM_VERIFIED_HOME": home / "verified",
        "HOME": home,
        "USERPROFILE": home,
        "APPDATA": home / "AppData/Roaming",
        "LOCALAPPDATA": home / "AppData/Local",
        "XDG_CONFIG_HOME": home / ".config",
        "XDG_CACHE_HOME": home / ".cache",
        "XDG_DATA_HOME": home / ".local/share",
        "TEMP": scratch,
        "TMP": scratch,
        "TMPDIR": scratch,
    }
    for key, value in values.items():
        env[key] = str(value)
    for key in (
        "RETROLARIUM_TEST_HOOKS",
        "RETROLARIUM_UNVERIFIED_GAMES",
        "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS",
        "RETROLARIUM_GAMES_SOURCE",
    ):
        env.pop(key, None)
    if packaged(studio):
        # Package acceptance uses bundled tools, never a source checkout.
        env.pop("RETROLARIUM_DEV", None)
        env.pop("RETROLARIUM_KIT", None)
    else:
        env["RETROLARIUM_DEV"] = "1"
        env["RETROLARIUM_KIT"] = str((kit or KIT_ROOT) / "rltool.py")
        if not env.get("RETROLARIUM_CORE"):
            core = build_core_executable(studio)
            if core is not None:
                env["RETROLARIUM_CORE"] = str(core)
    if source_archive is not None:
        # Only an explicit CLI choice may restore this override. Kit still
        # compares the complete unpacked tree with the scenario's pinned hash.
        env["RETROLARIUM_GAMES_SOURCE"] = str(source_archive.resolve())
    return env


def terminate_process_tree(process: subprocess.Popen) -> None:
    """Stop only the process tree created for this invocation."""
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        if process.poll() is None:
            process.kill()
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_process(executable: Path, scenario: Path, run_dir: Path, env: dict[str, str], timeout: int) -> dict:
    options = {}
    if os.name == "nt":
        options["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        options["start_new_session"] = True
    started = time.monotonic()
    process = subprocess.Popen(
        [str(executable), "--scenario", str(scenario)],
        cwd=run_dir,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        **options,
    )
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        terminate_process_tree(process)
        stdout, stderr = process.communicate()
    return {
        "exit": process.returncode,
        "timed_out": timed_out,
        "wall_seconds": round(time.monotonic() - started, 3),
        "stdout": stdout or "",
        "stderr": stderr or "",
    }


def file_evidence(path: Path) -> dict:
    evidence = {"path": str(path), "exists": path.is_file()}
    try:
        if evidence["exists"]:
            evidence.update(size=path.stat().st_size, sha256=sha256_file(path))
    except OSError as error:
        # Diagnostics must not replace the process's original failure if an
        # executable becomes unreadable while its identity is being recorded.
        evidence["read_error"] = f"{type(error).__name__}: {error}"
    return evidence


def git_identity(path: Path) -> dict:
    identity = {"path": str(path), "commit": None, "changes": []}
    if not (path / ".git").exists():
        return identity
    try:
        head = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        status = subprocess.run(
            ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=all"],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        if head.returncode == 0:
            identity["commit"] = head.stdout.strip()
        if status.returncode == 0:
            identity["changes"] = status.stdout.splitlines()
    except (OSError, subprocess.TimeoutExpired) as error:
        identity["error"] = str(error)
    return identity


def studio_version() -> str | None:
    try:
        text = (STUDIO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"project\s*\(\s*RetrolariumStudio\s+VERSION\s+([^\s\)]+)", text, re.S)
    return match.group(1) if match else None


def read_json(path: Path) -> dict | list | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def toolchain_evidence(kit: Path) -> dict:
    base = kit / "src/retrolarium_kit/core/toolchains"
    manifest_path = base / "manifest.json"
    manifest = read_json(manifest_path)
    host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    evidence = {"manifest": file_evidence(manifest_path), "release": None, "host": host, "suites": {}}
    if isinstance(manifest, dict):
        evidence["release"] = manifest.get("release")
        components = manifest.get("components", {})
        for suite, specification in manifest.get("suites", {}).items():
            entries = []
            for name in specification.get("components", []):
                component = components.get(name, {})
                files = []
                for item in component.get("hosts", {}).get(host, {}).get("files", []):
                    if item.get("executable"):
                        files.append(
                            dict(file_evidence(base / item["package_path"]), expected_sha256=item.get("sha256"))
                        )
                entries.append(
                    dict(id=name, version=component.get("version"), source=component.get("source"), executables=files)
                )
            evidence["suites"][suite] = entries
    # Retain the original report field for existing report consumers.
    evidence["nes_components"] = evidence["suites"].get("nes", [])
    return evidence


def provenance(studio: Path, kit: Path) -> dict:
    native_path = studio / "native-build.json"
    info_path = studio / "BUILD-INFO.txt"
    try:
        build_info = info_path.read_text(encoding="utf-8-sig") if info_path.is_file() else None
    except OSError as error:
        build_info = f"Could not read: {error}"
    return {
        "studio": {
            "path": str(studio),
            "kind": "packaged" if packaged(studio) else "source",
            "source_version": studio_version(),
            "executable": file_evidence(studio_executable(studio)),
            "native_helpers": {
                name: file_evidence(
                    studio_executable(studio).parent / "tools" / (name + (".exe" if os.name == "nt" else ""))
                )
                for name in ("rlcore", "retrolarium-flicky-level-writer")
            },
            "native_build": file_evidence(native_path),
            "native_build_json": read_json(native_path),
            "build_info": file_evidence(info_path),
            "build_info_text": build_info,
        },
        "sources": {
            "studio": git_identity(STUDIO_ROOT),
            "kit": git_identity(kit),
            "core": git_identity(CORE_ROOT),
            "core_pin": (STUDIO_ROOT / ".github/core-ref").read_text(encoding="utf-8").strip()
            if (STUDIO_ROOT / ".github/core-ref").is_file()
            else None,
        },
        "toolchains": toolchain_evidence(kit),
    }


def write_reports(run_dir: Path, report: dict) -> None:
    report["finished_at"] = utc_now()
    (run_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    status = str(report.get("status", "FAIL"))
    reason = str(report.get("stop_reason", ""))
    hashes = "".join(
        f"<tr><th>{html.escape(label)}</th><td><code>{html.escape(str(report.get(key) or '—'))}</code></td></tr>"
        for key, label in (
            ("input_sha256", "Input ROM SHA-256"),
            ("expected_sha256", "Approved output SHA-256"),
            ("actual_sha256", "Actual output SHA-256"),
        )
    )
    shots = []
    for shot in report.get("screenshots", []):
        name = str(shot.get("file", ""))
        if Path(name).name == name and (run_dir / name).is_file():
            shots.append(
                f'<figure><a href="{html.escape(name)}"><img loading="lazy" src="{html.escape(name)}"></a>'
                f"<figcaption>{html.escape(str(shot.get('step', name)))}</figcaption></figure>"
            )
    sections = []
    for title, value in (
        ("Studio messages", report.get("studio_messages", [])),
        ("Build log", report.get("build_log", "")),
        ("stdout", report.get("process", {}).get("stdout", "")),
        ("stderr", report.get("process", {}).get("stderr", "")),
        ("Events", report.get("events", [])),
        ("Executables after process exit", report.get("executables_after_process", {})),
        ("Provenance and toolchains", report.get("provenance", {})),
        ("Game source", report.get("game_source", {})),
    ):
        text = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False, indent=2)
        sections.append(f"<details><summary>{html.escape(title)}</summary><pre>{html.escape(text)}</pre></details>")
    page = (
        "<!doctype html><html lang=en><meta charset=utf-8>"
        f"<title>{html.escape(status)} — Studio GUI regression</title><style>"
        "body{font:14px system-ui;background:#16181b;color:#ddd;margin:24px;max-width:1400px}"
        f".status{{font-size:24px;font-weight:700;color:{'#8bd49c' if status == 'PASS' else '#e5b567' if status in ('BASELINE_PENDING', 'INSPECTED') else '#ef8b8b'}}}"
        "table{border-collapse:collapse}th,td{text-align:left;padding:6px 12px;border-bottom:1px solid #333}"
        "code,pre{font-family:Consolas,monospace;overflow-wrap:anywhere}pre{white-space:pre-wrap;background:#101214;padding:12px}"
        "details{margin:12px 0}summary{cursor:pointer;font-weight:600}.gallery{display:flex;flex-wrap:wrap;gap:12px}"
        "figure{margin:0;max-width:600px}img{max-width:600px;border:1px solid #444}figcaption{color:#aaa}a{color:#8bf}"
        "</style><h1>Studio real-widget GUI regression</h1>"
        f'<p class="status">{html.escape(status)}</p><p>{html.escape(reason)}</p>'
        f"<p>Last step: {html.escape(str(report.get('last_step') or '—'))}</p>"
        f"<table>{hashes}</table><div class=gallery>{''.join(shots)}</div>{''.join(sections)}</html>"
    )
    (run_dir / "report.html").write_text(page, encoding="utf-8", newline="\n")


def environment_report(env: dict[str, str]) -> dict:
    keys = (
        "RETROLARIUM_STUDIO_HOME",
        "RETROLARIUM_SETTINGS",
        "RETROLARIUM_VERIFIED_HOME",
        "RETROLARIUM_DEV",
        "RETROLARIUM_KIT",
        "RETROLARIUM_CORE",
        "HOME",
        "USERPROFILE",
        "APPDATA",
        "LOCALAPPDATA",
        "TEMP",
        "TMP",
        "TMPDIR",
    )
    result = {key: env[key] for key in keys if key in env}
    for key in (
        "RETROLARIUM_TEST_HOOKS",
        "RETROLARIUM_UNVERIFIED_GAMES",
        "RETROLARIUM_ALLOW_REPOSITORY_SCRIPTS",
        "RETROLARIUM_GAMES_SOURCE",
    ):
        result[key] = env.get(key, "<absent>")
    return result


def layout_verification_complete(report: dict) -> bool:
    checks = report.get("library_layout_checks", [])
    return (
        report.get("library_layout_contract") == 2
        and isinstance(checks, list)
        and all(
            isinstance(check, dict) and check.get("overlap_free") is True and not check.get("error") for check in checks
        )
    )


def presentation_verification_complete(report: dict) -> bool:
    project = report.get("scenario", {}).get("project", {})
    palette = project.get("kind") == "palettes" or project.get("editor") == "PaletteEditor"
    return layout_verification_complete(report) and (not palette or report.get("palette_presentation_verified") is True)


def validate_result(
    report: dict,
    process: dict,
    expected: str | None,
    rom: Path,
    run_dir: Path,
    inspect: bool = False,
    artifact_size: int | None = None,
) -> None:
    """Cross-check the native result, process status, input and optional artifact."""
    try:
        if sha256_file(rom) != report["input_sha256"]:
            report.update(
                status="FAIL",
                stop_reason="The input ROM changed during the GUI regression.",
            )
            return
    except OSError as error:
        report.update(
            status="FAIL",
            stop_reason=f"The input ROM could not be re-read after the run: {error}",
        )
        return
    if process["timed_out"]:
        report.update(
            status="FAIL",
            stop_reason="The GUI regression exceeded the runner process timeout.",
        )
        return
    status = report.get("status")
    if status not in (("INSPECTED", "FAIL") if inspect else ("PASS", "FAIL", "BASELINE_PENDING")):
        report.update(
            status="FAIL",
            stop_reason=f"Studio produced no valid result.json status (got {status!r}).",
        )
        return
    if process["exit"] != STATUS_EXIT[status]:
        report.update(
            status="FAIL",
            stop_reason=f"Result status {status} disagrees with process exit code {process['exit']}.",
        )
        return
    if status == "INSPECTED":
        if expected is not None or report.get("actual_sha256") or not report.get("inventory"):
            report.update(status="FAIL", stop_reason="Invalid read-only inspection report")
        return
    actual = report.get("actual_sha256")
    if status in ("PASS", "BASELINE_PENDING") and (not isinstance(actual, str) or not SHA256.fullmatch(actual)):
        report.update(
            status="FAIL",
            stop_reason="A completed build did not report a valid actual SHA-256.",
        )
        return
    if status == "PASS" and (expected is None or actual != expected):
        report.update(
            status="FAIL",
            stop_reason="PASS was reported without a matching approved SHA-256.",
        )
        return
    if status == "BASELINE_PENDING" and expected is not None:
        report.update(
            status="FAIL",
            stop_reason="BASELINE_PENDING was reported although an approved hash exists.",
        )
        return
    artifact_value = report.get("artifact_path")
    if artifact_value:
        artifact = Path(artifact_value).resolve()
        try:
            artifact.relative_to(run_dir.resolve())
            artifact_digest = sha256_file(artifact)
            actual_size = artifact.stat().st_size
        except (OSError, ValueError) as error:
            report.update(
                status="FAIL",
                stop_reason=f"The reported artifact is not readable inside this run: {error}",
            )
            return
        if artifact_size is not None and actual_size != artifact_size:
            report.update(
                status="FAIL", stop_reason=f"Built ROM container has {actual_size} bytes; expected {artifact_size}."
            )
            return
        if artifact_digest != actual:
            report.update(
                status="FAIL",
                stop_reason="The runner's artifact SHA-256 differs from Studio's result.",
            )
    elif artifact_size is not None and status in ("PASS", "BASELINE_PENDING"):
        report.update(status="FAIL", stop_reason="No ROM artifact was reported to check its declared container size.")

    if report.get("status") in ("PASS", "BASELINE_PENDING") and not presentation_verification_complete(report):
        report.update(
            status="FAIL",
            stop_reason="The native report lacks successful library geometry or visible palette verification.",
        )


VERIFY_GAME_SOURCE_PYTHON = r"""
import json
from pathlib import Path
import sys

sys.path.insert(0, sys.argv[1])
from retrolarium_kit.core import repository_trust

root = Path(sys.argv[2])
marker = repository_trust.read_marker(root)
if marker is None:
    raise RuntimeError(f"the selected Kit has no current verification record for {root}")
verified = repository_trust.verify_scripts(root)
if verified is None or verified != marker:
    raise RuntimeError("the prepared repository did not pass the selected Kit's script verification")
print(json.dumps({
    "record_path": str(repository_trust.record_path(root)),
    "root": marker["root"],
    "game": marker["game"],
    "repository": marker["repository"],
    "commit": marker["commit"],
    "content_sha256": marker["content_sha256"],
    "url": marker["url"],
    "script_count": len(marker["scripts"]),
    "scripts_sha256": repository_trust.scripts_digest(marker["scripts"]),
}))
"""


def verify_game_source(kit: Path, repository: Path, scenario: dict, env: dict[str, str]) -> dict:
    """Verify Kit's full-download record and current scripts against the scenario pin."""
    kit = kit.resolve()
    repository = repository.resolve()
    try:
        completed = subprocess.run(
            [
                sys.executable,
                "-B",
                "-c",
                VERIFY_GAME_SOURCE_PYTHON,
                str(kit / "src"),
                str(repository),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            timeout=120,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f"Could not run the selected Kit's source verification: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic output"
        raise RuntimeError(f"The selected Kit refused the prepared game source: {detail}")
    try:
        evidence = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"The selected Kit returned invalid source-verification JSON: {error}") from error
    expected = {
        "game": scenario["game"],
        "repository": scenario["source"]["repository"],
        "commit": scenario["source"]["commit"],
        "content_sha256": scenario["source"]["content_sha256"],
    }
    for key, pinned in expected.items():
        if evidence.get(key) != pinned:
            raise RuntimeError(
                f"Prepared game-source {key} is {evidence.get(key)!r}, but the scenario pins {pinned!r}."
            )
    record = Path(evidence["record_path"]).resolve()
    try:
        record.relative_to(Path(env["RETROLARIUM_VERIFIED_HOME"]).resolve())
    except ValueError as error:
        raise RuntimeError(f"The Kit verification record is outside this run's isolated home: {record}") from error
    return {
        "method": "selected Kit read_marker + verify_scripts",
        "kit": str(kit),
        "repository_root": evidence["root"],
        "game": evidence["game"],
        "repository": evidence["repository"],
        "commit": evidence["commit"],
        "content_sha256": evidence["content_sha256"],
        "url": evidence["url"],
        "record": file_evidence(record),
        "script_count": evidence["script_count"],
        "scripts_sha256": evidence["scripts_sha256"],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--roms",
        type=Path,
        required=True,
        help="only this folder is searched, by content",
    )
    parser.add_argument("--scenario", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=STUDIO_ROOT / "build/gui-regression")
    parser.add_argument("--studio", type=Path, default=STUDIO_ROOT / "build/win-amd64")
    parser.add_argument("--kit", type=Path, default=KIT_ROOT, help="Kit checkout for a source build")
    parser.add_argument(
        "--source-archive",
        type=Path,
        help="explicit local source ZIP; Kit still verifies its complete unpacked content",
    )
    parser.add_argument("--timeout", type=int, default=2400, help="whole GUI process timeout in seconds")
    parser.add_argument(
        "--inspect", action="store_true", help="Read-only tab/document inventory; exit 4, never a regression PASS"
    )
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    scenario = None
    run_dir = None
    try:
        scenario = load_scenario(args.scenario)
        if args.inspect != (scenario["edit"].get("operation") == "inspect"):
            raise ValueError("--inspect must be supplied exactly for an inspection scenario")
        if args.inspect and scenario["expected_sha256"] is not None:
            raise ValueError("An inspection cannot approve or compare an output baseline")
        studio = args.studio.resolve()
        kit = effective_kit(studio, args.kit)
        source_archive = args.source_archive.resolve() if args.source_archive else None
        run_dir = new_run_directory(args.out, scenario["id"])
        report = {
            "status": "FAIL",
            "last_step": "initialize isolated run",
            "stop_reason": "The run did not complete.",
            "input_sha256": scenario["input_sha256"],
            "expected_sha256": scenario["expected_sha256"],
            "actual_sha256": None,
            "started_at": utc_now(),
            "run_directory": str(run_dir),
            "scenario_path": str(args.scenario.resolve()),
            "scenario_id": scenario["id"],
            "game_source": dict(scenario["source"]),
            "provenance": provenance(studio, kit),
        }
        if source_archive is not None:
            report["game_source"]["archive"] = file_evidence(source_archive)
            report["game_source"]["network_download_covered"] = False
            if not source_archive.is_file():
                raise RuntimeError(f"The explicit source archive is not a file: {source_archive}")
        home = run_dir / "home"
        scratch = run_dir / "tmp"
        for path in (
            home,
            scratch,
            home / "Documents",
            home / "AppData/Roaming",
            home / "AppData/Local",
            home / ".config",
            home / ".cache",
            home / ".local/share",
        ):
            path.mkdir(parents=True, exist_ok=True)
        report["last_step"] = "select the unique ROM by content SHA-256"
        try:
            rom = find_rom(args.roms, scenario["input_sha256"])
        except InputError as error:
            report.update(
                status="MISSING_INPUT",
                stop_reason=f"{scenario['revision_name']} ({scenario['revision']}): {error}",
            )
            write_reports(run_dir, report)
            print(f"MISSING_INPUT: {report['stop_reason']}", file=sys.stderr)
            print(f"report: {run_dir / 'report.html'}")
            return STATUS_EXIT["MISSING_INPUT"]

        report["rom_path"] = str(rom)
        effective = dict(scenario)
        effective["rom_path"] = str(rom)
        effective["output"] = str(run_dir)
        effective_path = run_dir / "scenario.effective.json"
        effective_path.write_text(
            json.dumps(effective, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        env = environment(studio, home, scratch, kit, source_archive)
        report["environment"] = environment_report(env)
        if env.get("RETROLARIUM_CORE"):
            report["configured_core_tool"] = file_evidence(Path(env["RETROLARIUM_CORE"]))
        executable = studio_executable(studio)
        if not executable.is_file():
            raise RuntimeError(f"The opt-in GUI regression executable is missing: {executable}")
        report["last_step"] = "launch the real-widget Studio GUI"
        try:
            process = run_process(executable, effective_path, run_dir, env, args.timeout)
        finally:
            paths = {"gui_driver": executable}
            if env.get("RETROLARIUM_CORE"):
                paths["configured_core"] = Path(env["RETROLARIUM_CORE"])
            helper = (
                executable.parent
                / "tools"
                / ("retrolarium-flicky-level-writer.exe" if os.name == "nt" else "retrolarium-flicky-level-writer")
            )
            paths["flicky_level_writer"] = helper
            report["executables_after_process"] = {name: file_evidence(path) for name, path in paths.items()}
        report["process"] = process
        native = read_json(run_dir / "result.json")
        if not isinstance(native, dict):
            native = {}
        for key, value in native.items():
            if key not in ("status", "stop_reason"):
                report[key] = value
        if native.get("status") is not None:
            report["status"] = native["status"]
        if native.get("stop_reason") is not None:
            report["stop_reason"] = native["stop_reason"]
        if native.get("last_step"):
            report["last_step"] = native["last_step"]
        if native.get("project_path"):
            project = Path(native["project_path"]).resolve()
            report["game_source"]["prepared_project"] = str(project)
            try:
                project.relative_to(run_dir.resolve())
            except ValueError:
                pass
            else:
                for parent in project.parents:
                    if parent == run_dir.parent:
                        break
                    descriptor = parent / scenario["source"]["descriptor"]
                    if descriptor.is_file():
                        report["game_source"]["repository_path"] = str(parent)
                        report["game_source"]["descriptor_file"] = file_evidence(descriptor)
                        break
        validate_result(
            report, process, scenario["expected_sha256"], rom, run_dir, args.inspect, scenario.get("artifact_size")
        )
        if report["status"] in ("PASS", "BASELINE_PENDING", "INSPECTED"):
            report["last_step"] = "verify the prepared source with the selected Kit"
            repository = report["game_source"].get("repository_path")
            if not repository:
                raise RuntimeError("The prepared project has no identifiable pinned repository descriptor.")
            report["game_source"]["verification"] = verify_game_source(kit, Path(repository), scenario, env)
            report["last_step"] = "complete"
    except Exception as error:
        if run_dir is None:
            run_dir = new_run_directory(args.out, scenario["id"] if scenario else "invalid-scenario")
        if "report" not in locals():
            report = {
                "input_sha256": scenario.get("input_sha256") if scenario else None,
                "expected_sha256": scenario.get("expected_sha256") if scenario else None,
                "actual_sha256": None,
                "started_at": utc_now(),
                "run_directory": str(run_dir),
            }
        report.update(status="FAIL", stop_reason=f"{type(error).__name__}: {error}")
        report.setdefault("last_step", "initialize isolated run")
    write_reports(run_dir, report)
    status = report["status"]
    print(
        f"{status}: {report.get('stop_reason', '')}",
        file=sys.stderr if status == "FAIL" else sys.stdout,
    )
    print(f"report: {run_dir / 'report.html'}")
    return STATUS_EXIT.get(status, STATUS_EXIT["FAIL"])


if __name__ == "__main__":
    sys.exit(main())
