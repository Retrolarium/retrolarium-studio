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
  builtin_plugins.cpp   registers the linked plugins
resources/   bundled fonts (OFL) and the Qt resource file
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
| Sound › music & SFX | Sequencer | tracks as channels, drag pitch, Shift-drag length, mute, Listen (kit YMFM render, unsaved edits included) |
| Sound › FM voices, envelopes | Table | operator rows per voice; the 63 priority and envelope bytes |
| Playtest | Emulator | save, build, MAME embedded in the view at the chosen round; pause, stop, screenshot; a card shows build, start and stop progress |

Build (`Ctrl+B`) saves and runs `rltool studio build` with the plugin's level
writer; Play (`F5`) builds and opens Playtest at the current round. MAME is
`RETROLARIUM_MAME`, else `mame/consoles.exe` next to the studio, else the
nearest `mame_testing/mame/consoles.exe` up the tree; the kit is
`RETROLARIUM_KIT`, else the nearest `retrolarium_kit/rltool.py`, else `rltool`
on `PATH`.

MAME draws straight into the Playtest surface (`-attach_window`), so it never
sees its window as focused; the runner passes `-background_input`, without
which game pads are ignored. The kit's playtest script writes a status file
when the round is on screen (`FLICKY_PLAYTEST_RESULT`), which ends the
"Starting" card.

Flicky's font draws lowercase ASCII as kana; the Text inspector says so when
a string has lowercase letters.

Every edit is one step on the project undo stack; Save writes only the
changed documents; Revert returns a document to its state when it was opened
(levels: when the project was created).

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

## Fonts

`resources/fonts` bundles Barlow, Barlow Condensed and IBM Plex Mono from
[google/fonts](https://github.com/google/fonts) under the SIL Open Font
License 1.1 (`OFL-*.txt`).
