"""Import a conservative Qt runtime attribution inventory from the real SDK scanner.

Scan qtbase/src and qtsvg/src from the exact source archive first. The scanner
in older Qt ignores LicenseFiles arrays; those are recovered from the original
metadata so all license texts are retained. No Qt code/data is packaged here.
"""

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULES = {"qtcore", "qtgui", "qtwidgets", "qtsvg", "qtconcurrent"}
ALIASES = {"doubleconversion": "double-conversion", "libjpeg": "libjpeg-turbo"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--scanner-json", type=Path, required=True)
    parser.add_argument("--archive-sha256", required=True)
    parser.add_argument("--sdk-image", required=True)
    parser.add_argument("--longreach-commit", required=True)
    options = parser.parse_args()
    sources = options.sources.resolve()
    raw = {}
    for path in sources.rglob("qt_attribution.json"):
        document = json.loads(path.read_text(encoding="utf-8"), strict=False)
        for entry in document if isinstance(document, list) else [document]:
            raw[entry["Id"]] = (path.parent, entry)
    scanned = json.loads(options.scanner_json.read_text(encoding="utf-8"))
    destination = ROOT / "resources/licenses/qt-3rdparty"
    destination.mkdir(exist_ok=True)
    manifest = {
        "qt_version": "5.15.19",
        "reviewed_sdk_image": options.sdk_image,
        "scope": "Conservative notices for runtime sources in Core, Gui, Widgets, Svg and Concurrent; optional/platform-specific components may be unused after static linking.",
        "source": {
            "url": "https://download.qt.io/archive/qt/5.15/5.15.19/single/qt-everywhere-opensource-src-5.15.19.tar.xz",
            "sha256": options.archive_sha256,
        },
        "longreach_commit": options.longreach_commit,
        "components": [],
    }
    for item in scanned:
        if item.get("QDocModule") not in MODULES or "libs" not in item.get("QtParts", []):
            continue
        parent, original = raw[item["Id"]]
        files = original.get("LicenseFiles", [])
        if not files and original.get("LicenseFile"):
            files = [original["LicenseFile"]]
        copyright_text = original.get("Copyright", "")
        if isinstance(copyright_text, list):
            copyright_text = "\n".join(copyright_text)
        text = [
            item["Name"],
            "License: " + item["License"],
            copyright_text,
            "Corresponding source: " + manifest["source"]["url"],
            "",
        ]
        for name in files:
            path = (parent / name).resolve()
            if not path.is_relative_to(sources):
                raise ValueError(f"license outside source tree: {path}")
            text += ["--- " + name + " ---", path.read_text(encoding="utf-8"), ""]
        if not files and item["Id"] == "tika-mimetypes":
            text += [(sources / "qtbase/src/3rdparty/android/LICENSE").read_text(encoding="utf-8")]
        elif not files and "Public Domain" not in item["License"]:
            raise ValueError(f"missing license text for {item['Id']}")
        name = ALIASES.get(item["Id"], item["Id"]) + ".txt"
        output = destination / name
        output.write_text("\n".join(text).strip() + "\n", encoding="utf-8", newline="\n")
        manifest["components"].append(
            {
                "id": item["Id"],
                "name": item["Name"],
                "module": item["QDocModule"],
                "license": item["License"],
                "license_id": item.get("LicenseId", ""),
                "notice": name,
                "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                "source_path": parent.relative_to(sources).as_posix(),
            }
        )
    expected = {
        key
        for key, (_, entry) in raw.items()
        if entry.get("QDocModule") in MODULES and "libs" in entry.get("QtParts", ["libs"])
    }
    actual = {entry["id"] for entry in manifest["components"]}
    if expected != actual:
        raise ValueError(f"scanner omitted runtime metadata: {sorted(expected ^ actual)}")
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Recorded {len(actual)} runtime attribution entries and all their license texts.")


if __name__ == "__main__":
    main()
