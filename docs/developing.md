# Retrolarium Studio: running from source and packaging

This page is for people who build the studio themselves. Players need only the
user guide (`user_guide.md`).

## What a studio from a checkout looks for

The installed studio carries everything next to its program. Set
`RETROLARIUM_DEV=1` when running a source build with host tools. Without that
explicit flag, Python and Kit launchers are not discovered on PATH (nor is host
Python discovered through the Windows registry). Installed-package acceptance
must leave the flag unset.

A studio built from source finds its parts like this:

- **Retrolarium Kit**, which reads and writes the project and builds the ROM:
  the `RETROLARIUM_KIT` environment variable (`rltool` or `rltool.py`),
  `kit/rltool.py` next to the studio, a `../retrolarium-kit/rltool.py` up to six
  folders above the studio's own folder, then `rltool` on `PATH` in dev mode.
  `rltool.py` runs with `python/python.exe` next to the studio, else host Python
  in dev mode.
- **MAME** (`consoles.exe`): `RETROLARIUM_MAME`, the one chosen in File →
  Settings, or `mame/consoles.exe` next to the studio.
- **Game disassemblies**: Open ROM downloads them from the RetrolariumLab
  organisation on GitHub. For a test archive, set `RETROLARIUM_GAMES_SOURCE` to
  a URL or path with `{repository}` in it.

## Building

The default Core checkout must match `.github/core-ref`, which CI uses as well.
`-AllowCoreMismatch` is an explicit development override; a public package must
use the coordinated pin. Native provenance records the actual Core commit and
working changes. Update source dependency pins after committing Core and before
selecting the final release commits.

`tools\build-win.ps1` cross-compiles the studio, its helper programs and the
setup program with the static Qt 5 SDK of
[Longreach Qt](https://github.com/OpenKaryon/longreach-qt) in Docker (the
README's "Building from source" shows how to make its image).
`tools\build-mame-docker.ps1` builds the MAME the package ships the same way:
MinGW-w64 GCC cross-compiles the "consoles" subtarget (NES/Famicom, Vs. System,
SNES and Mega Drive drivers) of an unchanged MAME checkout in Docker, from
`tools\mame\Dockerfile`, at the commit `tools\mame\COMMIT` pins (the one place
it is named; `tools\build-mame.sh` and packaging read it too).
`tools\build-mame.sh` builds the same subtarget natively in MSYS2.

## The package and the installer

`tools\package-win.ps1` assembles `build\package\RetrolariumStudio`:

- the studio and the kit;
- a private Python taken from the CPython installation `-Python` names, trimmed
  and isolated;
- MAME with its license texts;
- every license text `THIRD-PARTY.md` lists;
- `BUILD-INFO.txt`, which names the commits the package was made from;
- `native-build.json`, the native build record, to which packaging adds the
  private Python's version and hashes and MAME's commit and hash.

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
python tools/test.py --suite native --bin build/win-amd64 --game-sources path/to/prepared-game-repositories --checks --history
python tools/test.py --suite python
powershell -ExecutionPolicy Bypass -File tools/package-win.ps1 -Python path/to/python -Mame path/to/mame/consoles.exe
python tools/release.py notes build/package
```

Review the native test output, including every skipped game fixture. Prepare
fresh Alien Soldier JP/EU projects through the kit's catalog and verify their
unchanged builds. Exercise editing and reopening before publishing support for
the new game.

`release.py notes` validates the ZIP's native files, Python and MAME against the
build record, unpacks the setup's payload and checks that it holds exactly the
ZIP's files, and writes `SHA256SUMS.txt` and `release-notes.md` from the actual
ZIP, setup and `BUILD-INFO.txt`. The release description starts with the
version's section of `CHANGELOG.md`: add it before packaging, or `notes` stops.
`package-win.ps1` writes the ZIP with `release.py zip` (sorted entries, `/` in
names, one fixed time), so the same package gives the same ZIP.

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
The build record names the image the build ran in by its ID. GNU ld has no LLVM
lld-style --dependent-load-flag option, including binutils 2.44. The static x64
MinGW installer supplies its PE load configuration directly. The build runs
tools/check_setup_binary.py on the emitted EXE and fails unless
DependentLoadFlags is exactly 0x800 (System32 only). This protects imports
before main() on Windows 10 RS1 and later; the runtime restriction also remains
in place.

The load-config directory must extend through CodeIntegrity (160 bytes on x64).
A legacy 112-byte prefix can contain the correct flag while Windows ignores it.
`check_setup_binary.py` rejects that prefix. The Windows native suite also runs
a real DLL-shadowing test: an unprotected control must load a local canary
`version.dll`, and a protected control must start without loading it. The MinGW
control uses the installer's actual load-config source.

GNU ld gained load-config directory emission in binutils 2.45; the original
SDK's 2.30 does not emit it. Before building, prepare_release_sdk.py resolves
the SDK image ID and prepares a separate cached GNU ld 2.45.1 layer if needed.
Its upstream source archive SHA-256 is pinned in tools/release-sdk/Dockerfile.
The first preparation needs network access; Qt, GCC and the base image stay
intact.

The host CI builds shared Qt with MSVC/GNU and does not replace release
acceptance. For every candidate, run the static MinGW build above and its
installed tests.json on Windows, then package and run release.py notes. Check
the app, helpers and setup import only Windows system DLLs, and test
install/update/uninstall on a clean Windows account with RETROLARIUM_DEV unset.
Keep the exact image ID, source commits, logs and skipped fixture list with the
candidate. Do not publish a dirty package or reuse older binaries.

Static Qt does not scan disk for platform plugins: Qt 5's
[QFactoryLoader::update](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/plugin/qfactoryloader.cpp)
wraps that scan in `QT_SHARED`. Setup uses the linked Windows plugin and clears
library paths after QApplication construction. The shared host CI SDK needs its
own platform plugin and is not the standalone installer distribution.

Scripted interface checks (python tools/autotest/run.py) keep their window off
the desktop and capture their own screenshots after loading each document. Use
the SDK's Windows platform backend; setting QT_QPA_PLATFORM=offscreen requires a
Qt plugin that this Windows SDK does not provide. The standalone --screenshot
option captures after a fixed delay and does not wait for project preparation;
use an interface scenario to verify an opened project.

## Updating translations

Run `python tools/update_translations.py` with the Longreach image, or pass
`--qt-prefix path/to/host/Qt` to use that SDK's lupdate. Use `--add locale` to
add a language. The tool recognizes rlTr as translate, keeps English plural
forms only, and leaves new translations unfinished for human review.

Changed or removed source strings keep their old translations as `vanished`
entries for translators; they are not included in the compiled catalogs. Do not
pass `-no-obsolete` during routine updates. Docker updates on POSIX run with the
host UID/GID so newly added catalogs are owned by the developer. Before a test
build, complete active translations and run `tests/test_translations.py` to
check text, plural forms and Qt placeholders. Linguistic and visual review is
still needed when a translation changes.

## Qt notices and corresponding sources

`resources/licenses/qt-3rdparty/manifest.json` records the reviewed Qt source
archive, SDK image ID, Longreach reference and hashes of all retained notices.
The inventory conservatively covers runtime source attributions in Core, Gui,
Widgets, Svg and Concurrent, including optional platform code. Notices for
unused optional components may remain; this avoids relying on linker stripping
to decide which copyright notices a package needs.

The current inventory was obtained with
`qtattributionsscanner --output-format json` from the actual static Qt 5.15.19
SDK, scanning `qtbase/src` and `qtsvg/src` from the exact Qt archive (SHA-256 is
in the manifest). Recreate it with
`python tools/update_qt_notices.py --sources PATH --scanner-json FILE --archive-sha256 HASH --sdk-image IMAGE_ID --longreach-commit COMMIT`.
The tool cross-checks all runtime metadata IDs and recovers `LicenseFiles`
arrays that the older scanner does not understand, including both JPEG texts.
Scanner warnings about newer CPE/PURL/comment fields are not taken as evidence
that a runtime component can be omitted.

`release.py package-notices` writes README.txt and THIRD-PARTY.txt and copies
all reviewed notices. It refuses missing/changed license texts or an SDK image
different from the reviewed inventory. Re-audit source/features when replacing
the SDK. `release.py notes` checks the resulting ZIP again and compares all
installer payload files with it. BUILD-INFO.txt and native-build.json remain
inside the package; public download assets remain installer, ZIP and SHA256SUMS.
