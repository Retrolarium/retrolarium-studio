# Private real-widget GUI regression

These are opt-in, ROM-dependent acceptance scenarios: **156 fixed scenarios
across all 18 supported revisions and seven game families**, each with an
explicitly reviewed output hash. A reviewed hash alone does not establish
acceptance; the local whole-catalog audit requires three independent current
PASS builds for every scenario. The scenarios complement `tools/autotest`
without replacing or weakening that suite. The older driver may call
`openRomFile`, model methods and `QAction::trigger`. This driver deliberately
does not: edits and navigation go through the widgets a person uses.

## Whole-catalog coverage

`tools/gui_regression/coverage.json` declares every game/revision from the
selected Kit's `core/profiles/repositories.json`, its exact input hash, actual
required tab IDs, observed Studio profile and suite manifest (or `null` while
unimplemented). The current catalog has seven game families and eighteen
revisions. Family IDs include SMB1's related Vs./FDS/ANN/SMB2 editions.

Exclusions are limited to Demo and Playtest. Moving another editable tab into
`excluded_kinds` invalidates the suite or catalog instead of claiming full
coverage. Use `--case` for an explicitly partial run.

`inventory.py` audits this declaration against the current catalog and the
owner's local ROM folder. A new catalog revision, changed input/source pin,
missing required tab or a suite attached to another revision requires an
explicit matrix update. It never substitutes another ROM edition. The ROM-free
Python CI job checks this matrix against the Kit commit in `.github/kit-ref`; it
checks out only source/catalog data and never receives private ROMs.

```powershell
python tools/gui_regression/inventory.py `
  --roms "C:\private-roms" `
  --kit "..\retrolarium-kit" `
  --reports "C:\gui-runs" `
  --out build/gui-coverage
```

The report distinguishes `NOT_IMPLEMENTED`, `BASELINE_PENDING`, `RUNS_REQUIRED`,
absent/ambiguous inputs and `COMPLETE`. `COMPLETE` requires every declared
scenario to have a reviewed golden and at least three distinct project runs
reporting `PASS` for that exact recipe/input/source, with the artifact still
readable and matching its hash. Duplicating one report does not create
independent evidence. Exit codes are 0 for complete coverage, 2 for an input
problem, 3 for incomplete coverage and 1/argparse 2 for invalid input or
configuration. Inspection reports never count as completed regressions.

The coverage report also counts and links historical failed attempts, including
earlier versions of a recipe. They keep their `FAIL` status when a later run
passes. The three-PASS requirement uses only valid, independent current-recipe
reports; historical failures remain visible in the audit.

For new fixture authoring, a separate read-only mode captures actual tab and
document IDs, editor types, inspector fields, semantic snapshots and library
items through visible GUI navigation:

```powershell
python tools/gui_regression/inspect_game.py `
  --game smb1 --revision pal `
  --roms "C:\private-roms" `
  --studio build/gui-native `
  --kit "..\retrolarium-kit" `
  --source-archive "C:\sources\pinned-smb1.zip" `
  --document world=physics `
  --out "C:\gui-inspection"
```

The game/revision and source pin come from the selected Kit catalog. By default,
one document per available included tab is inspected; repeated
`--document KIND=ID` arguments select explicit samples for that tab. Demo and
Playtest are always excluded. The project must remain unmodified. The result is
**`INSPECTED`, exit 4**, with no output hash, no ROM build and no regression
PASS. The lower-level `run.py --inspect` accepts only inspection recipes;
`run_suite.py` never accepts one as an acceptance scenario. Private inspection
recipes and observations stay in the selected local output directory.

Mega Drive imports need the matching `rlcore` executable for compressed art. The
opt-in `retrolarium-gui-regression` target depends on `rlcore`. For a source
checkout, the runner finds the Core CLI inside the selected CMake build and
records its path/hash. An explicit `RETROLARIUM_CORE` environment choice takes
precedence. The Core Python package checks its version. Tall pattern sheets have
their own scroll viewport, so 8x16 sprite libraries retain complete grid cells
and the last row remains reachable instead of being clipped. This is a host
dependency, not a reason to skip Tiles/Animations or replace the ROM.

Reports record executable identities before launch and again after the GUI
process exits: the GUI driver, selected Core CLI and Flicky level writer. A
missing or unreadable file remains diagnostic evidence; it never turns a failed
run into PASS or identifies which external program changed the file.

The inspection is preparation only: follow it with fixed GUI edits, exact
persistence assertions, a fresh ROM build, independent baseline review and three
clean repeat builds for each tab. Do not copy JU physics/note values into PAL:
its initial values differ. Each suite carries its own revision identity.

## Revision recipes

The matrix names all 18 revision suites with 156 scenarios: all seven SMB-family
revisions, both Pac-Man revisions, Castle Excellent/Castlequest, Doraemon, all
three Solomon's Key revisions, Flicky UE and Alien Soldier JP/EU. The scenarios
exercise every declared editable tab except Demo and Playtest. A fixture's
`expected_sha256` and `baseline` describe an explicitly reviewed output; use the
local inventory audit to distinguish approved hashes from three current PASS
builds. Authoring a recipe or recording three `BASELINE_PENDING` outputs does
not establish acceptance. New cases retain `expected_sha256: null` until full
GUI runs, screenshot/resource/ROM review and three independent clean builds
support an explicit review decision.

PAL physics and note pitch, Vs. palettes, FDS level-bank IDs, FDS text fields
and disk-image paths are fixed separately. The FDS reissue's platform badge was
corrected to `FDS` and confirmed by the native GUI acceptance runs. ANN and SMB2
physics cases visit the other bank and assert that its shared tables display the
same intended edit.

PC10/Vs. recipes declare `artifact_size: 49168`; the FDS recipes declare 65,500
bytes. Both the native driver and Python runner check declared container sizes,
so a truncated image cannot become a baseline candidate just because a build
exited successfully. Source preflight confirms that Pac-Man's fixed pellet edit
preserves its protected cells/connectivity and fits the 416-byte RLE stream; the
independent full-ROM review also checks its pellet count.

Castle Excellent/Castlequest have eight scenarios each. Room painting uses the
room's `cell:` card IDs; the metatile and screen cases use `tile:` cards. The
room edit fits both revisions' original prefix-code arenas in a data-only
capacity preflight. The ending text case explicitly declares its second, derived
table column in `edit.state_changes` with fixed `path`, `old` and `new` values.
Every declared precondition is checked before the edit; all other snapshot data
must remain unchanged. Duplicate paths, inferred expectations and missing
old/new values are rejected.

Both Castle music cases select Pulse 1 note `0:1`, `E` to `F` (pitch 64 to 65).
The original MML arena is completely full: `C` to `C#` needs an extra byte and
is rejected. This equal-length natural-note edit preserves every stream and
restart pointer. The same source note appears again in the loop at note IDs
`0:65` and `0:129`; both projections are fixed in `edit.state_changes`, while
all other notes remain unchanged. A data-only capacity or ROM-byte prediction is
preparation; it never counts as a GUI PASS or an approved baseline.

Doraemon has eight scenarios. Its big-block and small-block cases select actual
`block:` and `piece:` cards with `edit.library_item`; the title case changes a
written graphics byte, not a protected text or code-filled blank cell. Text
keeps the game's fixed width. Its first sound command is also played at note
`0:12`; that exact repeated projection is declared before editing.

Each Solomon's Key revision has seven scenarios. `map-toolbar-cell` selects the
Blocks layer and the Empty brush through actual toolbar segments. Its animation
uses `composition-inspector`: the Left/Right labels stay fixed, so the snapshot
also captures the selected piece's full inspector fields and verifies the
pattern pair change there. Palette template entries retain their original
format, while the group's marked overrides remain unchanged. The linked standing
poses update the left pattern of source frame 6 and the mirrored right pattern
of frame 9 together; the ROM review checks both bytes and preserves the other
frames. Each scenario still requires its own native GUI evidence and reviewed
output hash.

Flicky has seven scenarios. Its Layout collision brush, 16-pen pixel editor, RGB
channel picker, animation delay, title sprite position, fixed-width text and a
pinned pitched sound note use their actual GUI controls. The title sprite case
moves `sprite:statics:0` by eight pixels; its `object_positions` expectation
declares a one-cell horizontal translation and preserves every other object. The
SEGA tilemap's original 10-byte Enigma capacity is preserved. Source builds
publish `build/flicky-edited.bin`, not a revision-prefixed ROM filename.

Alien Soldier has ten scenarios per revision: Levels, Tiles, Metatiles,
Palettes, Objects, Bosses, Sprites, Screens, Text and Sound. The object-backed
metatile document is an unframed CompositionEditor; the piece's inspector and
derived label are both checked. The selected options-screen metatile is shared
by seven cursor pieces; its ROM review checks all seven references and their
unchanged size, positions, palette and end flags. The boss case extends the
first holding pose by one frame. Screens edits a written word in the independent
credits name table. JP/EU story strings and their encoded glyph columns are
pinned separately. The tile case changes its first pixel from pen 0 to white pen
2: pens 0 and 1 are both black in this art palette, so that earlier recipe could
not provide a visible before/after check. The Sound case uses a source byte
referenced only once in the inspected song; the earlier pitched byte is shared
twelve times and would change other notes. Both source builds publish
`build/alien-soldier-edited.bin`.

`palette-channel` selects an actual palette entry and RGB step. The snapshot
checks all palette colors, labels and selected channel values. Pixel pen clicks
use PenRow's real rectangles and confirm the selected pen; four-pen positions
are not reused for sixteen-pen tiles. `inspector-field` can select a fixed map
object or animation frame before checking its full inspector state.

The opt-in CMake target also depends on the Flicky level writer and places it
and `rlcore` in `tools/` beside the chosen configuration's driver, as required
by Studio's normal tool locator. Provenance includes both NES and Mega Drive
toolchain suites from the selected Kit, local executable hashes and native
helper paths/hashes. These declarations do not prove that a native ROM build has
run successfully.

Library geometry contract 2 covers the inspector's card widgets and painted
LibrarySheet grids, including offscreen rows. It checks complete IDs, positive
dimensions, containment and non-overlap. The driver clicks canonical item
rectangles in the intended sheet or inspector grid and records geometry hashes.
The runner and coverage auditor reject completed results lacking this contract
or containing a failed geometry check. The ROM-free native layout test includes
clipped, stale and duplicate-ID grids; the expanded native test passes.

Map-drag scenarios now exercise toolbar Undo/Redo as well. The coverage auditor
requires this evidence and any declared shared-document checks. Older JU reports
lack the expanded grid assertion, and its drag reports also lack Undo/Redo
evidence. Fresh JU and PAL 30/30 series now satisfy the stronger current
contract; the ten JU hashes are preserved. Previous PASS reports remain
historical evidence, not proof of new assertions.

## SMB1 JU coverage matrix

`smb1_ju_suite.json` declares nine required tabs and ten independent scenarios.
**Demo and Playtest are excluded**; neither their editors nor an emulator are
launched. The driver asserts the exact available tab set, so a newly added tab
requires an explicit coverage decision. Each case imports the original ROM into
its own home/project and produces its own edited-ROM SHA-256. Changes do not
accumulate between cases.

| Tab        | Scenario suffix     | Pinned change                                              |
| ---------- | ------------------- | ---------------------------------------------------------- |
| Levels     | `block`             | World 1-1 `ground_6`: coin block `(16,7)` → `(16,6)`       |
| Levels     | `level_settings`    | `ground_6`: timer `400` → `300`                            |
| World      | `world_physics`     | Physics: jump gravity standing `32` → `33`                 |
| Tiles      | `sprite_pixel`      | Sprites: tile 0 pixel `(0,0)` pen `0` → `1`                |
| Metatiles  | `metatile`          | Group 0 quadrant `(0,0)` pattern `$24` → `$25`             |
| Palettes   | `player_palette`    | Mario entry 1 `$16` → `$17`                                |
| Animations | `animation_pattern` | Big walk frame 2, cell 1: pattern `$08` → `$09`            |
| Screens    | `screen_pattern`    | Status bar cell `(3,2)`: `$16` → `$17` (`MARIO` → `NARIO`) |
| Text       | `text`              | Second player name `LUIGI` → `MUIGI`                       |
| Sound      | `music_note`        | `ground_part_1`, `square2:1`: pitch `72` → `73`            |

These are representative editing scenarios, not every control or document in
each tab. Explicit document switching exercises other levels, world tables,
graphics banks, metatile groups, palettes, animation groups, screens and songs.
The Text tab has one document. The animation case additionally switches frames;
the sound case switches channels and checks that note start/length and all other
notes remain unchanged. The common data-kind IDs are `graphics` for Tiles and
`sound` for Sound.

Each tab case uses mouse/keyboard events, checks an exact semantic snapshot
before and after the one declared edit, exercises toolbar Undo/Redo, switches
documents and verifies persistence after save/close/reopen. Inspector/table
values must also be visibly current before acceptance screenshots. Palette
checks wait for the actual selected swatch to be visible and painted with the
expected body color and selection outline; a model-only update cannot pass.
Snapshots cover all cells, pixels, palette entries, pieces in the selected frame
or notes in the selected song; final ROM comparison covers all serialized bytes.

All ten scenarios have manually reviewed output hashes and exact expected ROM
byte differences in their `baseline` metadata. The golden hashes were also
computed independently by applying the declared byte change to the original
input, then compared against three clean GUI builds. A successful build alone
does not approve a baseline. Each scenario changes one byte, preserves the
40,976-byte ROM size, and preserves every other byte.

## What the original block scenario exercises

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

Run the complete local matrix with three isolated repeats per case:

```powershell
python tools/gui_regression/run_suite.py `
  --roms "C:\path\to\private-roms" `
  --manifest tools/gui_regression/smb1_ju_suite.json `
  --out "C:\gui-runs" `
  --studio build/gui-native `
  --kit "..\retrolarium-kit" `
  --source-archive "C:\path\to\pinned-source.zip" `
  --repeat 3
```

Choose a **short absolute output path on Windows**: Kit's process working
directory includes the isolated project below it, and Windows/Qt process launch
can reject overly long paths even when Python can create the directories. The
suite uses compact `cNN-rNN` directories; `suite.json`/`suite.html` map them to
scenario IDs and per-run reports. Every GUI invocation still reserves a fresh
UUID home and project. `--case smb1_ju_text` selects a focused run; the summary
marks partial coverage. An unfinished series cannot claim full coverage.
Differing hashes between completed repeats fail the suite, including when a
scenario is still `BASELINE_PENDING`.

Private-ROM GUI runs are local only. Do not add them, private inputs, generated
projects, outputs or screenshots to GitHub Actions. Only ROM-free runner/matrix
unit tests run in general CI. Security/race suites are run on GitHub, not as a
side effect of these local GUI commands.

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

Run and suite directories use a short eight-character label and a random UUID
suffix. Full scenario/revision IDs and UTC timestamps stay in the JSON/HTML
reports. This reserves space for nested native assembler includes as well as the
source working directory: Python may create a path that a bundled native Windows
tool cannot open. Use a short local `--out` root on Windows as well.

Every invocation reserves a unique UUID directory. It contains the effective
scenario with private paths, Studio and Kit settings, Documents, prepared
project, compiler temporary files, verification records, screenshots,
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
hash, script count/digest and the selected Kit's NES/Mega Drive toolchain
manifest. Provenance and the toolchain manifest come from the selected `--kit`
for source builds and from `studio/kit` for packaged builds, never from an
unrelated default sibling. The input ROM is hashed again after the run.

Exit codes:

| Code | Status             | Meaning                                                                                   |
| ---- | ------------------ | ----------------------------------------------------------------------------------------- |
| 0    | `PASS`             | Fresh output matches an approved SHA-256 and the prepared source passes verification.     |
| 1    | `FAIL`             | A GUI step, build, freshness check, hash, source verification or runner invariant failed. |
| 2    | `MISSING_INPUT`    | The exact ROM is absent or ambiguous.                                                     |
| 3    | `BASELINE_PENDING` | The full path and source verification completed, but no output baseline is approved.      |

## Approve a baseline manually

The original block case is pinned to
`0b3e037e10e0c416379544db256d5a7d30a78ad423da00aa5daf6db0545d2f3c`. It differs
from the original only at ROM offset `0x26A0`: `07` → `06`. The nine additional
cases carry their own reviewed hashes. Neither the native driver nor either
runner approves or updates them. For a new scenario, begin with
`"expected_sha256": null` and follow this process:

1. Run the complete scenario and obtain `BASELINE_PENDING`.
2. Inspect the run's before/after/reopened screenshots, before/after object
   records, Studio messages, build log, source provenance and actual ROM.
   Confirm the scenario's exact semantic edit and unchanged unrelated data.
   Decode the changed ROM addresses using the pinned source profile and confirm
   the expected byte differences independently; do not approve an arbitrary
   observed output just because it repeats.
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
