"""Refresh Qt translation sources with a local SDK or the Longreach image."""

import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def command(root, language, qt_prefix=None, image="qt5-win-amd64:latest"):
    if re.fullmatch(r"[A-Za-z][A-Za-z0-9_@-]*", language) is None:
        raise ValueError("invalid translation language")
    args = ["-tr-function-alias", "translate+=rlTr"]
    if language == "en":
        args.append("-pluralonly")
    args += [
        "-locations",
        "none",
        "-no-obsolete",
        "-target-language",
        language,
        "src",
        "plugins",
        "installer/src",
        "-ts",
        f"resources/i18n/retrolarium_studio_{language}.ts",
    ]
    if qt_prefix is not None:
        executable = "lupdate.exe" if sys.platform == "win32" else "lupdate"
        return [str(qt_prefix / "bin" / executable), *args]
    return [
        "docker",
        "run",
        "--rm",
        "--entrypoint",
        "/usr/local/Qt-5.15/bin/lupdate",
        "--mount",
        f"type=bind,source={root.as_posix()},target=/studio",
        "--workdir",
        "/studio",
        image,
        *args,
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-prefix", type=Path, help="a host Qt SDK containing bin/lupdate")
    parser.add_argument("--image", default="qt5-win-amd64:latest")
    parser.add_argument("--add", action="append", default=[], help="additional locale (repeatable)")
    args = parser.parse_args(argv)
    languages = {path.stem.removeprefix("retrolarium_studio_") for path in (ROOT / "resources/i18n").glob("*.ts")}
    languages.update(args.add)
    for language in sorted(languages):
        subprocess.run(command(ROOT, language, args.qt_prefix, args.image), cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
