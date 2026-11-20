# Retrolarium Studio: running from source and packaging

This page is for people who build the studio themselves. Players need only the
user guide (`user_guide.md`).

## What a studio from a checkout looks for

The installed studio carries everything next to its program. A studio built from
source finds its parts like this:

- **Retrolarium Kit**, which reads and writes the project and builds the ROM:
  the `RETROLARIUM_KIT` environment variable (`rltool` or `rltool.py`),
  `kit/rltool.py` next to the studio, a `../retrolarium-kit/rltool.py` up to six
  folders above the studio's own folder, then `rltool` on `PATH`. `rltool.py`
  runs with `python/python.exe` next to the studio, else Python on `PATH`.
- **MAME** (`consoles.exe`): `RETROLARIUM_MAME`, the one chosen in File →
  Settings, or `mame/consoles.exe` next to the studio.
- **Game disassemblies**: Open ROM downloads them from the RetrolariumLab
  organisation on GitHub. For a test archive, set `RETROLARIUM_GAMES_SOURCE` to
  a URL or path with `{repository}` in it.

## Building

`tools\build-win.ps1` cross-compiles the studio, its helper programs and the
setup program with the static Qt 5 SDK of
[Longreach Qt](https://github.com/OpenKaryon/longreach-qt) in Docker (the
README's "Building from source" shows how to make its image).
`tools\build-mame-docker.ps1` builds the MAME the package ships the same way:
MinGW-w64 GCC cross-compiles the "consoles" subtarget (NES/Famicom, Vs. System,
SNES and Mega Drive drivers) of an unchanged MAME checkout in Docker, from
`tools\mame\Dockerfile`, and `tools\mame\build.sh` pins the commit.
`tools\build-mame.sh` builds the same subtarget natively in MSYS2.

## The package and the installer

`tools\package-win.ps1` assembles `build\package\RetrolariumStudio`:

- the studio and the kit;
- a private Python taken from the CPython that `python` runs, trimmed and
  isolated;
- MAME with its license texts;
- every license text `THIRD-PARTY.md` lists;
- `BUILD-INFO.txt`, which names the commits the package was made from.

It also writes the folder's zip and `RetrolariumStudio-<version>-setup.exe`: the
installer (`installer/`, in the studio's own frame and style) with that folder
attached and checked file by file. It refuses a working tree with uncommitted
changes, unless `-AllowDirty` is given to try a package out. A missing license
text makes it warn that the package is not ready to publish.

The setup program:

- asks for its language (the studio then starts in it);
- installs for the current user without administrator rights;
- keeps the Disk System BIOS and MAME's settings across updates;
- adds Start menu (and optionally desktop) shortcuts, an Installed Apps entry
  and the `.rlj` association;
- copies itself into the installation as `Uninstall.exe`, which removes all of
  that again, together with the kit's unpacked assemblers. Your games in
  `Documents\Retrolarium` stay unless you ask.

## Preparing a release

Use clean Studio, Kit and Core checkouts. The native build records their source
identity and hashes of the app, installer and helpers in
`build/win-amd64/native-build.json`. Packaging rejects an older executable, a
changed helper or a native source checkout that changed since the build.
`-AllowDirty` still permits a local trial package, whose provenance says so.

```powershell
python ../retrolarium-kit/tools/check_game_repositories.py
powershell -ExecutionPolicy Bypass -File tools/build-win.ps1 -QtKit path/to/longreach-qt
powershell -ExecutionPolicy Bypass -File tools/test-win.ps1 -GameSources path/to/prepared-game-repositories
python -m unittest discover -s tests -p test_release.py
powershell -ExecutionPolicy Bypass -File tools/package-win.ps1 -Mame path/to/mame/consoles.exe
python tools/release.py notes build/package
```

Review the native test output, including every skipped game fixture. Prepare
fresh Alien Soldier JP/EU projects through the kit's catalog and verify their
unchanged builds. Exercise editing and reopening before publishing support for
the new game.

`release.py notes` validates the ZIP's native files against its build record and
writes `SHA256SUMS.txt` and `release-notes.md` from the actual ZIP, setup and
`BUILD-INFO.txt`.

Upload only these three files as GitHub release assets:

- `RetrolariumStudio-<version>-setup.exe`;
- `RetrolariumStudio-<version>-win64.zip`;
- `SHA256SUMS.txt`, covering those exact installer and ZIP files.

Use `release-notes.md` as the release description, not as an attached asset.
Keep `BUILD-INFO.txt` and `native-build.json` inside the installer and ZIP; do
not upload them separately. Build logs, test reports and other technical files
also stay out of the release assets.

Publish `SHA256SUMS.txt` for every release, including older releases when their
published files can be verified. Derive its hashes from the actual published
binaries or their verified GitHub asset digests. Adding a checksum file does not
require replacing existing installers or ZIPs.

If published binaries are replaced, update both `SHA256SUMS.txt` and the release
description to match them. A release body must describe its own packaged
versions rather than the latest source tree.

The default Windows SDK image is `qt5-win-amd64:latest`. `build-win.ps1 -Image`
selects another compatible SDK, and `-Out` selects an independent build folder.

Scripted interface checks (python tools/autotest/run.py) keep their window off
the desktop and capture their own screenshots after loading each document. Use
the SDK's Windows platform backend; setting QT_QPA_PLATFORM=offscreen requires a
Qt plugin that this Windows SDK does not provide. The standalone --screenshot
option captures after a fixed delay and does not wait for project preparation;
use an interface scenario to verify an opened project.
