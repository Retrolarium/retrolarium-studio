"""Build and test Studio with the host Qt SDK, using Python on both CI hosts.

This checks portability with a native shared Qt build. Release packaging
continues to use the separate static Longreach Qt SDK.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def run(command: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", type=Path, default=Path(os.environ.get("RUNNER_TEMP", "build")) / "ci-qt")
    parser.add_argument("--setup-only", action="store_true")
    parser.add_argument("--jobs", default="2")
    args = parser.parse_args()
    prefix = None
    if sys.platform == "win32":
        qt_root = args.qt_root.resolve()
        prefix = qt_root / "5.15.2/msvc2019_64"
        if not (prefix / "lib/cmake/Qt5/Qt5Config.cmake").is_file():
            qt_root.mkdir(parents=True, exist_ok=True)
            run([sys.executable, "-m", "pip", "install", "aqtinstall==3.3.0"])
            run(
                [
                    sys.executable,
                    "-m",
                    "aqt",
                    "install-qt",
                    "windows",
                    "desktop",
                    "5.15.2",
                    "win64_msvc2019_64",
                    "--outputdir",
                    str(args.qt_root.resolve()),
                    "--archives",
                    "qtbase",
                    "qtsvg",
                    "qttools",
                    "qttranslations",
                ],
                cwd=qt_root,
            )
    elif sys.platform == "linux":
        os.environ.setdefault("DEBIAN_FRONTEND", "noninteractive")
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        sudo = [] if os.geteuid() == 0 else ["sudo"]
        run([*sudo, "apt-get", "update", "-qq"])
        run(
            [
                *sudo,
                "apt-get",
                "install",
                "-y",
                "--no-install-recommends",
                "cmake",
                "g++",
                "make",
                "qtbase5-dev",
                "libqt5svg5-dev",
                "qttools5-dev",
                "qttools5-dev-tools",
            ]
        )
    else:
        parser.error("automatic CI SDK setup supports Windows and Linux; use tools/test.py --qt-prefix on other hosts")
    if not args.setup_only:
        command = [sys.executable, str(Path(__file__).with_name("test.py")), "--suite", "native", "--jobs", args.jobs]
        if prefix is not None:
            command += ["--qt-prefix", str(prefix)]
        run(command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
