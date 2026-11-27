# Private real-widget GUI regression

This is a separate, ROM-dependent acceptance scenario for **Super Mario Bros.
NES, revision JU only**. It complements `tools/autotest`; it does not replace or
weaken that suite. The older driver may call `openRomFile`, model methods and
`QAction::trigger`. This driver deliberately does not: edits and navigation go
through the widgets a person uses.

## What it exercises

`retrolarium-gui-regression` creates the real `MainWindow` and registers the
same builtin game and platform plugins as Studio. It shows the window on the
desktop and uses Qt Test input events to:

1. choose **File → Open ROM** and type the selected ROM path into the real Qt
   file dialog;
2. click **Set up the source**, then allow the pinned repository scripts
   whenever Kit requests consent; a ready project is not blocked when Kit
   requests none;
3. select the Levels kind and `ground_6` (World 1-1) in the document list; this
   single-bank project omits the `levels/` document prefix;
4. drag the existing single `Question block (coin)` from `(16, 7)` to `(16, 6)`
   in the `MapCanvas`, using the Select tool and real press/move/release events;
5. verify exactly that coordinate change, unchanged remaining objects and shared
   byte budget, and the unsaved-change indicator;
6. save from the toolbar, close through the File menu, reopen through the real
   project file dialog and verify that the object survived;
7. remove only the previous `build/smb1-ju-edited.nes` inside this run's
   project, build from the toolbar and hash the fresh artifact.

Model access is read-only and is used for bounded waits, assertions and
coordinates derived from the visible editor. A `QTimer` started before the
scenario independently handles the Qt file dialog, **Set up the source** and
**Run scripts** consent. It keeps running inside each dialog's nested `exec()`
loop; the scenario never schedules a click for a dialog that its own blocked
loop must close first. Unexpected modal dialogs fail the run.

There are no `RETROLARIUM_TEST_HOOKS`, direct model mutations, direct
document-opening calls or action triggers. A previous artifact that exists but
cannot be removed is an immediate failure, so it cannot be mistaken for a fresh
build.

The non-native **Qt** file dialog is covered. The native Windows file dialog is
explicitly outside this scenario's coverage. The scenario also does not play the
built ROM or replace emulator, visual and manual gameplay acceptance.

## Build the opt-in driver

The option is OFF by default. Qt Test is found only when it is enabled. The
executable is not installed, added to CTest or included in a package, and the
ordinary Studio target does not acquire a Qt Test dependency.

Configure with the same Qt/toolchain choices as a normal Studio build, adding:

```text
-DRETROLARIUM_BUILD_GUI_REGRESSION=ON
```

For example, with a host Qt 5.15 development environment:

```powershell
cmake -S . -B build/gui-native -DRETROLARIUM_BUILD_GUI_REGRESSION=ON
cmake --build build/gui-native --target retrolarium-gui-regression --config Release --parallel
```

Pass either the build root or the folder containing
`retrolarium-gui-regression.exe` as `--studio`. The runner checks the root,
`.cmake-build`, and the `Release`, `RelWithDebInfo`, `MinSizeRel` and `Debug`
subdirectories below both. Thus an MSVC build works with
`--studio build/gui-native` or the explicit `--studio build/gui-native/Release`.
For a `tools/build-win.ps1` layout, the runner likewise accepts
`build/win-amd64` or its `.cmake-build` folder. Keep the Core checkout at
`.github/core-ref` and the Kit checkout beside Studio.

## Run

From the Studio checkout:

```powershell
python tools/gui_regression/run.py `
  --roms "C:\path\to\private-roms" `
  --scenario tools/gui_regression/smb1_ju_block.json `
  --out build/gui-regression `
  --studio build/win-amd64
```

Use `--studio build/gui-native` for the example host build above. `--kit`
selects another Kit checkout for a source build. For offline source preparation,
add `--source-archive "C:\path\to\pinned-source.zip"`. Only this explicit CLI
choice restores `RETROLARIUM_GAMES_SOURCE` after inherited overrides are
cleared. Kit still unpacks the archive and verifies the complete tree against
the pinned content SHA-256; a mismatch is refused. The archive path and ZIP hash
are recorded, and `network_download_covered` is `false`. `--timeout` bounds the
whole GUI process (2400 seconds by default); the scenario JSON has separate
open, editor, save, reopen and build timeouts.

The runner recursively examines files **only below `--roms`**, regardless of
name or extension, and selects exactly one file whose complete SHA-256 is
`f61548fdf1670cffefcc4f0b7bdcdd9eaba0c226e3b74f8666071496988248de`. It does not
scan disks or download a ROM. No match or multiple matches is `MISSING_INPUT`.
Studio itself downloads the pinned public game source through the GUI, or
imports the explicit local archive through that same GUI path.

Every invocation reserves a unique timestamp/UUID directory. It contains the
effective scenario with private paths, Studio and Kit settings, Documents,
prepared project, compiler temporary files, verification records, screenshots,
`result.json`, `report.json` and `report.html`. Earlier runs are never removed.
Source builds use `RETROLARIUM_DEV=1` and the selected Kit; packaged runs clear
developer mode and external Kit selection. `RETROLARIUM_VERIFIED_HOME` is inside
the run. Inherited test hooks, unverified-repository consent, repository-script
consent and game-source overrides are removed, so they cannot bypass the GUI
consent or source checks. A timeout terminates only the process tree created by
this runner.

After a native `PASS` or `BASELINE_PENDING`, a child Python process loads the
**same effective Kit** and calls `read_marker(repository)` and
`verify_scripts(repository)`. The record must bind this canonical repository
root to the current Kit catalog, and its game, repository, commit and content
SHA-256 must equal the scenario pin. Current executable script hashes must still
match that record. A missing/stale record, changed script or pin mismatch turns
the result into `FAIL`. This verifies Kit's record of the full pristine tree
comparison; it does not falsely re-hash the prepared tree, which now contains
the ROM, generated project and build files.

The report includes the last step, stop reason, input/expected/actual hashes,
Studio messages, build log, stdout/stderr, screenshots, executable hashes,
`native-build.json` and `BUILD-INFO.txt` when present, Studio/Kit/Core source
identities, the pinned and verified game-source identity, verification-record
hash, script count/digest and the NES toolchain manifest. Provenance and the
toolchain manifest come from the selected `--kit` for source builds and from
`studio/kit` for packaged builds, never from an unrelated default sibling. The
input ROM is hashed again after the run.

Exit codes:

| Code | Status             | Meaning                                                                                   |
| ---- | ------------------ | ----------------------------------------------------------------------------------------- |
| 0    | `PASS`             | Fresh output matches an approved SHA-256 and the prepared source passes verification.     |
| 1    | `FAIL`             | A GUI step, build, freshness check, hash, source verification or runner invariant failed. |
| 2    | `MISSING_INPUT`    | The exact ROM is absent or ambiguous.                                                     |
| 3    | `BASELINE_PENDING` | The full path and source verification completed, but no output baseline is approved.      |

## Approve a baseline manually

`smb1_ju_block.json` starts with `"expected_sha256": null`. Neither the native
driver nor the runner approves or updates it.

1. Run the complete scenario and obtain `BASELINE_PENDING`.
2. Inspect the run's before/after/reopened screenshots, before/after object
   records, Studio messages, build log, source provenance and actual ROM.
   Confirm that only the fixed coin question block moved from `(16, 7)` to
   `(16, 6)`, with all other objects and pool usage unchanged.
3. Repeat from two new empty output roots. Both are clean runs with independent
   homes and projects. Require the same actual SHA-256 in all three successful
   runs and investigate any difference rather than choosing one arbitrarily.
4. Only after that review, manually replace `expected_sha256` in a separate,
   reviewed change with the observed hash. Later runs return `PASS` only for
   that exact fresh artifact. A changed result is `FAIL`, not a new baseline.

The ROM, extracted assets, generated projects, built artifacts and screenshots
remain under the ignored `build/` output or another owner-selected output
folder. Do not add them to Git.

## Add a second game

Do not turn this SMB-specific path into a claim of generic game coverage. Add a
new scenario JSON and, where its editor differs, a separately reviewed native
driver path. Pin the exact game/revision name and complete input SHA-256, source
repository descriptor/ref/commit/content hash, project profile, kind and
document, library item or editor-specific edit, target cell/coordinates and
project-relative artifact. Add appropriate widget names and real input events,
then verify persistence and artifact freshness. Begin with a null output hash
and follow the same manual screenshot/provenance/two-clean-repeat approval
procedure. ROM-dependent runs stay out of general CI; only synthetic runner unit
tests belong there.

## Why the first edit moves an existing block

The original JU source uses 3372 of 3373 shared object bytes. Adding a two-byte
object would exceed that pool. The fixed vertical move retains the record's
column, page, type and size and changes only its row; no budget checks are
relaxed. The driver verifies the exact object list and budget again after
saving, closing and reopening. A warning or error during the edit fails
immediately with Studio's diagnostic instead of waiting for a missing object.

## Inspector layout checks

Before every normal screenshot the GUI driver checks the live library-card
rectangles, including cards below the scroll viewport. The exact card set must
match the model; cards must have valid dimensions, remain inside their content
container, and never intersect one another. The check neither activates layouts
nor waits for a later good frame. Its observations are saved as
`library_layout_checks` in the run report.

The ROM-free `inspector_layout_test` exercises the first painted frame, the
first event pass, selection rebuilding, scroll-position preservation, switching
to a short library, and clearing the inspector at three panel sizes. It is
included in the normal native test manifest; the private ROM scenario remains
opt-in.
