# Retrolarium Studio

The new Retrolarium desktop editor, written from scratch in Qt 5.15 Widgets
(C++17). It replaces the application in `../retrolarium`, which stays in the
repository as a reference until its game features are ported as plugins.

The look follows the Claude Design prototype
`../retrolarium_design/Retrolarium Studio v4.dc.html` and its Industry design
system: steel-blue accent, Barlow / Barlow Condensed / IBM Plex Mono, square
corners, hairlines and corner registration marks, in dark and light themes.

## Principle

The studio knows nothing about games. The shell and the universal editors
(TileMap, Tile, Palette, Composition, Table, Sequencer, Emulator) are
configured by a game profile. Everything platform- or game-specific (formats,
codecs, renderers, build and playtest steps) lives in `plugins/`.

## Layout

```text
src/
  frame/     studio window and dialogs: own title bar on the native Windows frame
  theme/     design tokens, fonts, style sheet, QProxyStyle, icons, painting helpers
  widgets/   reusable themed widgets (buttons, segments, chips, fields, lists, tabs)
  sdk/       plugin contracts: project file, plugin, session, editor models
  runtime/   host services: one kit/tool process at a time, kit and MAME lookup,
             MAME embedded into a widget, WAV playback
  editors/   universal editors working only through sdk models (TileMap, Tile,
             Palette, Table, Composition, Sequencer, Emulator)
  shell/     main window: toolbar, kind rail, document list, editor host,
             inspector, bottom panel, status bar
  gallery/   theme gallery: every component on one page, the visual test bench
  app/       entry point; the only code that names concrete plugins
plugins/
  megadrive/ Mega Drive platform: VDP patterns, CRAM colors, tile words,
             Enigma (over ../retrolarium_core); Qt-free
  flicky/    Flicky (UE): data/ documents and codecs, render/ scene renderer,
             plugin/ session and editor models, tools/ level writer
  nes/       NES platform: master palette, palette packet layout; the resource
             bundle and the CHR and palette-table editors NES games share
  castle/    Castle Excellent / Castlequest: rooms, painter, tables
  solomon/   Solomon's Key: rooms, painter
  smb/       Super Mario Bros.: data/ resource bundle and area catalog,
             render/ level renderer and painter, plugin/ session and models,
             tests/ the renderer's golden test
  builtin_plugins.cpp   registers the linked plugins
resources/   bundled fonts (OFL), the brand set and the Qt resource file
tools/       Docker build and run scripts
```

The CMake targets enforce the layering: plugins link `studio_sdk` only, the
shell never links a plugin, and only the app links both.

| Library | May use |
|---|---|
| `studio_ui` (theme, widgets, frame) | Qt |
| `studio_sdk` | Qt Core/Gui (+ QUndoStack from Widgets) |
| `studio_runtime` | sdk, Qt Core/Gui/Widgets, Win32 |
| `studio_editors` | ui, sdk, runtime |
| `studio_shell` | ui, sdk, editors, gallery |
| `platform_megadrive` | nothing |
| `flicky_data` | megadrive, Qt Core/Gui |
| `flicky_plugin` | flicky_data, sdk |

## How a plugin plugs in

1. The shell reads the `.rlj` with `sdk::ProjectFile` and asks
   `sdk::PluginRegistry` for a plugin whose `canOpen()` accepts it.
2. `Plugin::open()` returns an `sdk::Session`: profile, rail kinds, document
   lists, and one project-wide `QUndoStack`.
3. For a picked document the session returns an `sdk::EditorModel` subclass
   (`TileMapModel`, …). The matching universal editor draws it; the inspector
   is generated from `EditorModel::inspect()`.
4. Every edit is a command on the session's undo stack; saving and dirty marks
   are the session's job.
5. Build, Play and previews go through `sdk::Host` (`runProcess`,
   `launchEmulator`, `playAudio`, `locateKit`, …). A plugin describes a
   process as an `sdk::ProcessSpec`; the shell runs it, streams stderr to
   Build output and hands stdout back (the kit answers in JSON).

## Flicky status

Ported: all five workspace documents, the collision codec, the level source
writer (`retrolarium-flicky-level-writer`) and the scene renderer.

| Kind | Editor | State |
|---|---|---|
| Levels | TileMap | Objects / Layout / Background layers; place, drag, remove, retype objects; paint collision |
| Tiles | Tile | 8 banks (4bpp and fonts): pencil, fill, pick, preview palette from the round CRAM or any palette |
| Tiles › sega_tilemap | TileMap | 12×4 name table, words edited in the inspector, live Enigma size against the 10-byte slot |
| Tiles › sega_palette | Palette | 10 compact CRAM entries, slot and 3-bit RGB |
| Palettes | Palette | 28 CRAM palettes, 3-bit RGB picker, rounds drawn with each palette |
| Levels › bonus rounds | TileMap | the scene simulated at a chosen frame; drag the seesaws, tigers and chick spawns (shared by all 12 bonus rounds) |
| Text | Table | 52 fixed-slot strings with length bars, validation and a game-style preview |
| Sprites › mappings | Composition | cropped to the sprite and zoomed to fit; pieces drawn with a round's palette; drag a piece, the mirrored `flipped_x` follows |
| Sprites › animations | Composition | frame strip at the game's delay, play, reorder frames, delay in the inspector |
| Sprites › bonus motion | Table | raw motion words with their meaning |
| Sound › music & SFX | Sequencer | tracks as channels, drag pitch, Shift-drag length, mute, Listen (kit YMFM render, unsaved edits included), Loop to repeat it until Stop |
| Sound › FM voices, envelopes | Table | operator rows per voice; the 63 priority and envelope bytes |
| Playtest | Emulator | save, build, MAME embedded in the view at the chosen round; pause, stop, screenshot; a card shows build, start and stop progress |

Build (`Ctrl+B`) saves and runs `rltool studio build` with the plugin's level
writer. Play from selected (`F5`) builds and opens Playtest at the selected
start point: an editor sets it (`EditorModel::startPoint()`, shown as the
"Start point" chip; for Flicky, the round opened in Levels) and the session
keeps it while other documents are open. Play (`Ctrl+F5`) runs the game from
power-on (`rltool studio launch-plan`). Pause (`Pause`) and Stop
(`Shift+F5`) sit next to them and work only while the game runs. MAME is
`RETROLARIUM_MAME`, else `mame/consoles.exe` next to the studio, else the
nearest `mame_testing/mame/consoles.exe` up the tree; the kit is
`RETROLARIUM_KIT`, else the nearest `retrolarium_kit/rltool.py`, else `rltool`
on `PATH`.

MAME draws straight into the Playtest surface (`-attach_window`), so it never
sees its window as focused; the runner passes `-background_input`, without
which game pads are ignored. Its keyboard providers cannot read the keyboard of a
window another process owns, so the surface takes the keys itself while it
has focus (it gets it when the game is up, or on a click) and the runner
writes them, by MAME's names, to a keys file the control script reads each
frame; the script presses every I/O port field whose MAME input assignment
names one of them. Studio shortcuts and menus do not see game keys (Alt and
Alt+Space do not open menus); function keys and Pause stay with the studio. The kit's playtest script writes a status file
when the round is on screen (`FLICKY_PLAYTEST_RESULT`), which ends the
"Starting" card.

Starting fast:

- MAME runs from a temporary folder of the runner: a copy of the cartridge
  (MAME locks the file it runs, so the build can replace the original while
  the game plays) and a control script that MAME runs as its autoboot
  script, with the game's own `-autoboot_script` inside it unchanged.
- When a launch names a ready file (`ProcessSpec::readyFile`), the control
  script runs MAME unthrottled and muted until the file appears, and the
  Playtest view covers the picture, so the boot and the steering to the start
  point pass unseen in a fraction of a second. A launch can instead give a
  ready condition (`ProcessSpec::readyWhen`, a Lua expression with `memory`
  the main CPU's program space); the control script checks it each frame and
  writes the ready file itself after three frames in a row.
- The control script reads `picture.txt` while the game runs: Pixels
  Square scales the 4:3 TV screen MAME assumes back to square pixels
  (render container `xscale`/`yscale`), Colours Studio writes the palette a
  launch gives (`ProcessSpec::palette`, NES: the editors' 64 colours) into
  the machine's palette device. Keys the window keeps (Alt, Ctrl) get
  aliases: Z and X press buttons 1 and 2, Enter start, right Shift select.
- Hot reload: Play while the same game runs (same program, working
  directory and arguments apart from the cartridge and the autoboot script)
  keeps the process. The runner stages a copy of the new build, rewrites the
  control script with the launch's environment and ready file, and leaves a
  command the script picks up within a few frames; it mounts the copy
  (`image:load`), which hard-resets the machine into the new build. MAME
  reads the autoboot script again on that reset. Frame callbacks survive a
  hard reset in MAME (notifiers and the legacy `emu.register_frame_done`
  list), so the control script routes the game script's callbacks through
  subscriptions it drops on the next run.
- Flicky skips the kit when nothing changed. A fingerprint of the build
  inputs (size and time of every project file outside `build/` and the level
  writer) and the emulator commands planned for that build are kept in
  `build/studio-build.json`, also across sessions. An unchanged project plays
  in about a second; an explicit Build always runs the kit.

Pause suspends MAME. The last frame, taken from the compositor
(`PrintWindow` with `PW_RENDERFULLCONTENT`; a suspended process hands it
nothing), is shown in black and white until Resume. Screenshots use the same
capture.

Flicky's font draws lowercase ASCII as kana; the Text inspector says so when
a string has lowercase letters.

Every edit is one step on the project undo stack; Save writes only the
changed documents; Revert returns a document to its state when it was opened
(levels: when the project was created).

## Super Mario Bros. status

Projects come from `rltool studio preset <rom>`; every editor reads and
writes `documents/resources.json`, the NES resource bundle. The level
renderer is the original app's, pinned to the kit's output for all 34 SMB1
JU areas by `smb_level_renderer_golden_test` (run it with
`plugins/smb/tests/smb1_ju_level_scenes.json`). `smb_area_streams_test` checks
the stream edits (remove and reinsert every record of every area) against the
same file.

| Kind | Editor | State |
|---|---|---|
| Levels | TileMap | areas drawn as the game does (metatiles in the area palette, enemies, Hero), day or night, pages of 16 columns; move, add (library) and delete objects and enemies, edit their kind (with previews), size and row and the area header, within the area's stream bytes (SMB1 JU areas are full; with a pooled resource, `capacity_pool`, all levels share the kit's object and enemy ranges, so bytes freed in one level serve another); drag Hero, or drop the library's Test start, where Play from selected begins |
| World | Table | the route of every world (the area each course step plays, numbered as the game shows levels; a selected step is the Play from selected start) and the nine player physics tables (gravity, jump speeds, top speeds, friction, climbing) |
| Tiles | Tile | sprite and background pattern tables, 2bpp, preview palette from the area packets or the player palettes |
| Metatiles | TileMap | the 16x16 background blocks of each palette group, four patterns each; paint quadrants from the pattern library, take a pattern with the pick tool, type numbers, or copy a whole block |
| Palettes | Palette | area packets (eight palettes of four), the packets the game writes over them (snow skies, mushroom platforms, Boss) previewed where they show, and the player table with Hero, Brother and fire previews; the 64-colour NES picker |
| Animations | Composition | Hero's animations (big and small walk, swim, climb, ...), each its frames of 2x4 sprite patterns in the Hero, Brother or fire colours, with the cells the engine flips drawn flipped; set a cell's pattern from the library; World > Player actions picks the frame each action shows |
| Screens | TileMap | the status bar, lives, time-up, game-over and warp-zone screens as the PPU display lists leave them; paint tiles from the pattern sheet and 16x16 palettes into the cells the lists write (a repeated write changes its run); on the Writes layer drag a whole write elsewhere or turn it to run down, byte for byte the same list; edit their text fields. With a pooled resource (SMB1 JU) the four game screens share their 110 bytes, so one list may grow while another shrinks |
| Text | Table | every fixed text: the fixed fields and the screens' text fields, in the game's font, within each slot; in SMB1 JU the status headings, TIME UP and GAME OVER are as long as typed, up to the end of their row, taking bytes other screen texts give up |
| Sound | Sequencer | every music header (area music, ground theme parts, events) on the piano roll: square 2, square 1, triangle, noise; drag notes to change pitch, square 1 and drum lengths in place, run lengths and beats in the inspector, which names every song sharing a byte; Play renders the song with the kit (`audio render-smb-wav`); Loop repeats it until Stop |
| Playtest | Emulator | Play from selected starts at Hero's cell (`smb-point-playtest-plan`), Play from power-on |

Play from selected starts hidden and fast: the kit's point script reports
nothing, so the plugin hands the runner a ready condition (`readyWhen`, a Lua
expression over the main CPU's memory: the area running with Hero on the
start page) and the control script writes the ready file itself. The
swim/stomp envelope has no editor yet.

MAME shows a "known problems" screen for the NES driver that waits for a key,
which never reaches an embedded MAME. Before a launch the runner marks it as
seen in `cfg/<system>.cfg` (`ui_warnings`, with the features `-listxml`
reports), so MAME's `skip_warnings` setting skips it.

## Castle Excellent status

Projects from `rltool studio preset` on Castle Excellent (J) or
Castlequest (U).

| Kind | Editor | What it does |
|---|---|---|
| Rooms | TileMap | 100 rooms of 20x20 cells (16x8 px, two tiles each) coloured by the attribute bytes the loader writes; Cells layer paints from the cell sheet, Objects layer moves, adds and removes static objects (24 per room, 2449-byte stream, 141 bytes of saved state) and the start marker for Play from selected |
| Tiles | Tile | both CHR banks, sprite and background tables |
| Palettes | Palette | room and screen palette tables |
| Text | Table | the sub-command menu words (A-Z and blank) |
| Sound FX | Table | the sound-effect parameter bytes |

## Solomon's Key status

Projects from `rltool studio preset` on Solomon's Key (USA or Europe).

| Kind | Editor | What it does |
|---|---|---|
| Rooms | TileMap | 53 rooms of 16x12 cells drawn as the game builds them; Blocks paints brown and white blocks, Items moves, adds and removes items (named from the source reconstruction's catalog; hidden or in a brown block) and moves the door, key, mirrors and constellation, Enemies does the same for enemies (named with direction and speed; at most 17) and Wizard's start; the room inspector sets the tileset, timer speed, Hornhead lifetime and constellation; budgets count the shared item (1342 B) and enemy (726 B) arenas |
| Mirrors | Table | the Spawn Mirror enemy sets (42 B shared) and drop schedules, with the rooms that use each |
| Animations | Composition | every action's frame cycle; drop a pattern pair on a half, flip it or change its palette; the action's delay |
| Screens | TileMap | the two packed title layers on the $2800 name table; paint the cells a layer writes with tiles $80-$FF |
| Sound | Sequencer | the 26 audio programs by group; notes per virtual channel as the engine steps them, drag to change a note's byte; Listen renders with the kit (`audio render-solomon-wav`) |
| Demo | Table | the attract demo's 34 input steps |
| Tiles, Palettes | Tile, Palette | the four CHR banks and the room and ending palettes |

## Libraries and pattern sheets

A model offers things to place or paint with as an `sdk::Library`. Where it
shows is the model's choice, and switching is one line: return it from
`EditorModel::sheet()` to put it in the work area beside the canvas (sized to
pick from), or set it as `Inspection::library` to put it in the inspector's
right column. `Library::grid` draws either as a tile sheet sixteen to a row
instead of labelled entries. Both places pick on click and drag onto the
canvas; a map model can also let the right button pick
(`TileMapModel::rightClickPicks`). SMB puts its pattern tables beside the
canvas (Metatiles, Screens, Animations) and its level objects in the
inspector.

## Theme rules

- Colors come only from `theme::Tokens` (`src/theme/tokens.*`). Widgets read
  `theme::current()` in `paintEvent` and repaint on `Theme::changed`.
- Canvas overlays that sit on game pixels (selection, object tags, the
  checkerboard) use `theme::canvas::*` and stay the same in both themes.
- The style sheet (`src/theme/stylesheet.cpp`) is generated from the tokens.
  Widgets opt into roles with dynamic properties (`surface`, `divider`,
  `tone`, `variant`, `badge`, `field`) instead of object names.
- Qt style sheets cannot express letter spacing or uppercase, so fonts are
  never set in the sheet. Use `theme::font(TextRole, cssPixels)` or
  `ui::Label` roles.
- Icons are Lucide-style paths rendered by `theme::icon(name, tone)`; their
  color follows the theme and the button state. Plugins can add glyphs with
  `theme::registerIcon`.
- Dialogs derive from `frame::StudioDialog` (messages: `frame::MessageDialog`),
  never `QMessageBox`, so they carry the same title bar.
- Top-level windows derive from `frame::StudioWindow`. Its title bar holds the
  app mark, the menus, a centred title and the window buttons. On Windows the
  native frame stays (Aero Snap, shadow, resize borders, maximize animation,
  Windows 11 snap layouts on the maximize button): only the caption is
  removed in `WM_NCCALCSIZE` and `WM_NCHITTEST` maps the bar back to caption,
  resize and maximize hits. Use `menus()` rather than `QMainWindow::menuBar()`.

## Build and run

Requires Docker with the `qt5-win-amd64-gpl-enabled` image and
`longreach-qt` next to this repository (same as the old application).

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-win.ps1
powershell -ExecutionPolicy Bypass -File tools\run-win.ps1 -Theme dark
```

Open a project directly, optionally at a document:

```powershell
build\win-amd64\retrolarium-studio.exe path\to\flicky-ue.rlj --document levels/round-01
```

A Flicky test project is created with the kit (it only reads the source
checkout and ROM):

```powershell
python ..\retrolarium_kit\rltool.py studio flicky-create "<rom>" --source-root <flicky_src> --project-root build\testdata\flicky
```

Keep project paths short: the source snapshot is deep and Windows path
limits apply.

The studio and the gallery (`--gallery`) can render themselves off screen, which is how the look is checked
without opening a window:

```powershell
build\win-amd64\retrolarium-studio.exe --theme light --size 1600x2700 --screenshot gallery.png
```

`Ctrl+Shift+L` or View → Toggle theme switches the theme at runtime.

## Brand

`resources/brand` is the Retrolarium brand set ("Scanline R", see its
README): SVG and pixel-snapped PNG icons, the Windows `.ico`, the macOS
`.icns` and the Linux hicolor tree for packaging. The studio uses the `.ico`
for the executable, the accent tile as the window and taskbar icon in the
light theme and the dark tile in the dark one, and the mark (`mark.svg` on
light, `mark-on-dark.svg` on dark) in the title bar, the welcome page and
About; `theme/brand.hpp` hands them out for the current theme.

## Fonts

`resources/fonts` bundles Barlow, Barlow Condensed and IBM Plex Mono from
[google/fonts](https://github.com/google/fonts) under the SIL Open Font
License 1.1 (`OFL-*.txt`).
