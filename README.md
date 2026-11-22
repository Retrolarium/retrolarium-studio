# Retrolarium Studio

Retrolarium Studio changes classic console games from their disassemblies. Open
your own ROM: the studio recognises the game and version, downloads the game's
disassembly, and gives you editors that show each part the way the game draws
it:

- levels and worlds, tiles and blocks, palettes;
- sprites and animations, screens, text;
- music, sound effects and the attract demo.

Build writes a new ROM. Play runs it inside the studio in MAME, from power-on or
right where you are editing, and a running game reloads into each new build.
File → Create IPS patch shares the result.

| Game                                                                                                     | Platform                  |
| -------------------------------------------------------------------------------------------------------- | ------------------------- |
| Super Mario Bros. (Japan/USA, Europe, PlayChoice-10, Disk System, All Night Nippon, Super Mario Bros. 2) | NES / Famicom Disk System |
| Castle Excellent                                                                                         | NES                       |
| Solomon's Key                                                                                            | NES                       |
| Pac-Man                                                                                                  | NES                       |
| Doraemon                                                                                                 | NES                       |
| Flicky                                                                                                   | Mega Drive                |
| Alien Soldier (Japan, Europe)                                                                            | Mega Drive                |

Alien Soldier requires Studio 0.2.0 or a current source build. It offers stage
maps and objects, tiles, metatiles, palettes, sprite mappings and animations,
screens, text, music and sound effects, and the four attract demos. Art and
sound edits retain the original payload capacities. The game's
[editing guide](https://github.com/RetrolariumLab/alien_soldier_src/blob/main/docs/retrolarium.md)
describes verified previews and format limits.

The interface is in English, Brazilian Portuguese, Spanish, simplified Chinese,
Japanese, French, German, Italian, Polish and Russian.

## Download

Windows 10 or 11 (64-bit). Download the setup program from the Releases page and
run it; it needs no administrator rights. Check the release's game list and
version: the older 0.1.0 package supports the six games listed before Alien
Soldier. Everything the studio needs comes with the package:

- the Retrolarium Kit, which builds the ROMs, and a private Python that runs it;
- MAME;
- OpenDiskSysROM, a free Famicom Disk System BIOS.

You bring only your ROMs. The studio is not signed yet: the user guide says what
to do when Windows warns about that.

## Documentation

- [User guide](docs/user_guide.md): installing, opening a game, the views, what
  each game offers, troubleshooting.
- [Running from source and packaging](docs/developing.md).
- [Architecture](docs/architecture.md): the plugin model, the source layout, the
  theme rules.
- [Game status](docs/game_status.md): each game's integration in detail.

## Building from source

Requires Docker and the static Qt 5.15 SDK for Windows from
[Longreach Qt](https://github.com/OpenKaryon/longreach-qt) (v0.1.0 or newer),
cross-compiled with MinGW-w64. Clone it beside this repository and build its
image `qt5-win-amd64` once, which needs GNU make and Python 3.11 or newer and
takes an hour or more:

```sh
git clone --branch v0.1.0 https://github.com/OpenKaryon/longreach-qt.git
make -C longreach-qt qt5-win-amd64
```

`tools\build-win.ps1` finds `longreach-qt` beside any folder above the studio;
`-QtKit` or `RETROLARIUM_QT_KIT` names another checkout and `-Image` another
image. Clone [retrolarium-core](https://github.com/Retrolarium/retrolarium-core)
and [retrolarium-kit](https://github.com/Retrolarium/retrolarium-kit) beside
this repository too: the build compiles the core from there, and the studio runs
the kit for every build and Play.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-win.ps1
python tools/test.py --suite native --bin build/win-amd64 --checks --history
powershell -ExecutionPolicy Bypass -File tools\run-win.ps1 -Theme dark
```

The build installs into `build\win-amd64` what the CMake files install: the app,
the helper programs plugins need next to it and the plugins' test programs. A
plugin registers a test with `studio_add_test(target fixture...)` (fixtures
relative to this directory); CTest runs them on a native build and
`python tools/test.py --suite native --bin build/win-amd64` runs them from the
generated `tests.json` on Windows; the same runner supports native Linux and
macOS builds. A test whose game fixture is missing exits with code 77 and counts
as skipped, not passed.

Open a project directly, optionally at a document:

```powershell
build\win-amd64\retrolarium-studio.exe path\to\flicky-ue.rlj --document levels/round-01
```

A Flicky test project is created with the kit (it only reads the source checkout
and ROM):

```powershell
python ..\retrolarium-kit\rltool.py studio flicky-create "<rom>" --source-root <flicky_src> --project-root build\testdata\flicky
```

Keep project paths short: the source snapshot is deep and Windows path limits
apply.

The studio and the gallery (`--gallery`) can render themselves off screen, which
is how the look is checked without opening a window:

```powershell
build\win-amd64\retrolarium-studio.exe --theme light --size 1600x2700 --screenshot gallery.png
```

`Ctrl+Shift+L` or View → Toggle theme switches the theme at runtime.

## Contributing

Bug reports and pull requests are welcome. A change to a game's editor should
keep a build of the unchanged project byte-identical to the original ROM: the
kit's tests check that. Interface text goes through `tr()`, and new text needs
its translations in every `resources/i18n/*.ts` (`tools/update_translations.py`
brings them in). Run `make hooks` once per clone: Git then checks every commit
and push for stop words and runs `make lint` and `make format-check` first (see
Retrolarium Core's README, "Stop words and Git hooks").

## Trademarks

The games are named only to say which ones Retrolarium works with. Their titles
and characters are trademarks of their owners, who have not made or endorsed
Retrolarium. Retrolarium does not include the games: you need your own copy of
each.

## License

Retrolarium Studio is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version (`GPL-3.0-or-later`, text in `LICENSE`). It comes without any
warranty.

The Windows package carries other components under their own licenses;
`THIRD-PARTY.md` lists them and where their texts are in the package.

## Portable test runner

The same entrypoint runs on Windows, Linux and macOS with Python 3.11 or later.
Clone the current `retrolarium-core` beside the other products (or install a
Core that includes `retrolarium_core.testing`). The runner selects the
checkout's sources explicitly. No PowerShell or Bash wrapper is needed. Python
suites use Git for repository fixtures. Native suites additionally need CMake
and the chosen build tool, such as MSBuild, Ninja or Make. The CI matrix covers
Windows and Linux.

```text
python tools/test.py
python tools/test.py --suite python
python tools/test.py --suite checks
python tools/test.py --suite checks --format --history
```

The default runs all tests this product has. Core and Studio also build and run
their native tests with CMake and a host C++ compiler. The Kit and Disassembler
have Python suites. `--suite python` needs no compiler or Qt. Repository guards
and formatting are separate opt-in checks. The test runner explicitly excludes
the PowerShell formatter, so it never probes or starts PowerShell. The other
formatters retain their own language-specific tools; the separate `make format`
command still supports PowerShell files. A private stop-word list comes from
`RETROLARIUM_STOPWORDS`, an ancestor `.stopwords`, or CI's `STOPWORDS_LIST`. A
missing unconfigured list is reported as skipped; a configured missing file
fails. `--history` still checks historical objects.

Native test failures and timeouts fail the command. Missing game fixtures and a
test's exit code 77 count as skips, never passes. Missing executables or tracked
repository fixtures are failures. `--game-sources PATH` selects the prepared
game repositories; paths with spaces work without shell quoting inside the
runner. `--jobs`, `--config` and `--build-dir` control CMake. Windows, POSIX and
filesystem-specific assertions still run on their own platforms; the common
runner does not pretend another host has those APIs.

```text
python tools/test.py --suite native
python tools/test.py --suite native --bin path/to/existing/test-binaries
```

`--bin` runs an existing `tests.json` suite without rebuilding. Core's native
suite includes the real Python-to-rlcore round trips. Studio needs Qt 5.15 Core,
Gui, Widgets, Svg, Concurrent and LinguistTools; `--qt-prefix PATH` selects its
SDK and supplies Qt's DLL/plugin paths. A native headless Linux Studio run uses
Qt's offscreen plugin. Windows retains the Windows platform plugin.

GitHub Actions builds and tests native Studio on Windows and Linux as well as
running Python tests on both. `tools/ci_native.py` prepares a shared host Qt SDK
for those checks (Windows Qt 5.15.2/MSVC; Linux distribution Qt 5.15). It is a
CI setup helper that installs dependencies; ordinary local test runs use
`tools/test.py` and an existing SDK. Release builds continue to use the static
Longreach Qt SDK and the existing release packaging tools.

The SMB native checks use original synthetic fixtures in public CI, and the
common malformed-document checks require no game corpus. Full ROM comparisons
are a separate local command: see [SMB test data](docs/testing_smb.md).

## Core revision in CI

CI checks out the full Core commit recorded in `.github/core-ref`, so rerunning
an older revision uses the same dependency. `python tools/core_pin.py` validates
and prints the pin. To update it, test the intended Core commit with this
checkout, put its full lowercase SHA in that file, and commit the update.
Publish the Core commit before publishing a dependent pin update.

The make entrypoints use `python` on Windows and `python3` elsewhere;
`make PYTHON=...` selects an explicit interpreter.
