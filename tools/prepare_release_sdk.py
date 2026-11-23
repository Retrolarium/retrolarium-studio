"""Select a Longreach SDK whose linker emits PE load-config directories."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def output(*args: str) -> str:
    return subprocess.check_output(["docker", *args], text=True).strip()


def supports_load_config(version: str) -> bool:
    # GNU ld learned _load_config_used in 2.45. Verify the actual PE after
    # linking as well; recognizing a version is not proof of the output.
    match = re.search(r"GNU ld[^\n]*? (\d+)\.(\d+)(?:\.\d+)?(?:[ \n]|$)", version)
    if not match:
        raise ValueError("The release SDK needs GNU ld with a known version")
    return tuple(map(int, match.groups())) >= (2, 45)


def linker_version(image_id: str) -> str:
    linker = output("run", "--rm", "--entrypoint", "x86_64-w64-mingw32-g++", image_id, "-print-prog-name=ld")
    if not linker or "\n" in linker:
        raise ValueError("The SDK compiler did not identify its linker")
    return output("run", "--rm", "--entrypoint", linker, image_id, "--version")


def prepare(image: str) -> str:
    # Resolve first: a tag changing during the build must not change the SDK.
    image_id = output("image", "inspect", "--format", "{{.Id}}", image)
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", image_id):
        raise ValueError("Docker did not return an SDK image ID")
    version = linker_version(image_id)
    if supports_load_config(version):
        return image_id
    base = "retrolarium-studio-sdk-base:" + image_id[7:]
    # BuildKit FROM needs a named local reference rather than a bare image ID.
    subprocess.run(["docker", "tag", image_id, base], check=True, stdout=sys.stderr)
    name = "retrolarium-studio-sdk:binutils-2.45.1-" + image_id[7:]
    subprocess.run(
        ["docker", "build", "--build-arg", f"QT_SDK={base}", "--tag", name, str(ROOT / "release-sdk")],
        check=True,
        stdout=sys.stderr,
    )
    result = output("image", "inspect", "--format", "{{.Id}}", name)
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", result):
        raise ValueError("Docker did not return the prepared SDK image ID")
    if not supports_load_config(linker_version(result)):
        raise ValueError("The prepared SDK compiler still uses an unsupported linker")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default="qt5-win-amd64:latest")
    args = parser.parse_args()
    try:
        print(prepare(args.image))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Could not prepare release SDK: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
