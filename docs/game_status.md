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
  frame/     studio window: own title bar on the native Windows frame
  theme/     design tokens, fonts, style sheet, QProxyStyle, icons, painting helpers
  widgets/   reusable themed widgets (buttons, segments, chips, fields, lists, tabs)
  sdk/       plugin contracts: project file, plugin, session, editor models
  editors/   universal editors working only through sdk models (TileMap, Tile,
             Palette, Table so far)
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
| `studio_editors` | ui, sdk |
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
| Text | Table | 52 fixed-slot strings with length bars, validation and a game-style preview |
| Sprites, Sound, Playtest | — | documents listed; editors next |

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
