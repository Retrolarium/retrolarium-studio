# Retrolarium Studio: architecture

How the studio is put together, for people changing its code. Building it is
described in `developing.md`.

The look follows the Industry design system: steel-blue accent, Barlow / Barlow
Condensed / IBM Plex Mono, square corners, hairlines and corner registration
marks, in dark and light themes.

## Principle

The studio knows nothing about games. The shell and the universal editors
(TileMap, Tile, Palette, Composition, Table, Sequencer, Emulator) are configured
by a game profile. Everything platform- or game-specific (formats, codecs,
renderers, build and playtest steps) lives in `plugins/`.

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

| Library                             | May use                                 |
| ----------------------------------- | --------------------------------------- |
| `studio_ui` (theme, widgets, frame) | Qt                                      |
| `studio_sdk`                        | Qt Core/Gui (+ QUndoStack from Widgets) |
| `studio_runtime`                    | sdk, Qt Core/Gui/Widgets, Win32         |
| `studio_editors`                    | ui, sdk, runtime                        |
| `studio_shell`                      | ui, sdk, editors, gallery               |
| `platform_megadrive`                | nothing                                 |
| `flicky_data`                       | megadrive, Qt Core/Gui                  |
| `flicky_plugin`                     | flicky_data, sdk                        |

## How a plugin plugs in

1. The shell reads the `.rlj` with `sdk::ProjectFile` and asks
   `sdk::PluginRegistry` for a plugin whose `canOpen()` accepts it.
2. `Plugin::open()` returns an `sdk::Session`: profile, rail kinds, document
   lists, and one project-wide `QUndoStack`.
3. For a picked document the session returns an `sdk::EditorModel` subclass
   (`TileMapModel`, …). The matching universal editor draws it; the inspector is
   generated from `EditorModel::inspect()`.
4. Every edit is a command on the session's undo stack; saving and dirty marks
   are the session's job.
5. Build, Play and previews go through `sdk::Host` (`runProcess`,
   `launchEmulator`, `playAudio`, `locateKit`, …). A plugin describes a process
   as an `sdk::ProcessSpec`; the shell runs it, streams stderr to Build output
   and hands stdout back (the kit answers in JSON).

## Libraries and pattern sheets

A model offers things to place or paint with as an `sdk::Library`. Where it
shows is the model's choice, and switching is one line: return it from
`EditorModel::sheet()` to put it in the work area beside the canvas (sized to
pick from), or set it as `Inspection::library` to put it in the inspector's
right column. `Library::grid` draws either as a tile sheet sixteen to a row
instead of labelled entries. Both places pick on click and drag onto the canvas;
a map model can also let the right button pick
(`TileMapModel::rightClickPicks`). SMB puts its pattern tables beside the canvas
(Metatiles, Screens, Animations) and its level objects in the inspector.

## Theme rules

- Colors come only from `theme::Tokens` (`src/theme/tokens.*`). Widgets read
  `theme::current()` in `paintEvent` and repaint on `Theme::changed`.
- Canvas overlays that sit on game pixels (selection, object tags, the
  checkerboard) use `theme::canvas::*` and stay the same in both themes.
- The style sheet (`src/theme/stylesheet.cpp`) is generated from the tokens.
  Widgets opt into roles with dynamic properties (`surface`, `divider`, `tone`,
  `variant`, `badge`, `field`) instead of object names.
- Qt style sheets cannot express letter spacing or uppercase, so fonts are never
  set in the sheet. Use `theme::font(TextRole, cssPixels)` or `ui::Label` roles.
- Icons are Lucide-style paths rendered by `theme::icon(name, tone)`; their
  color follows the theme and the button state. Plugins can add glyphs with
  `theme::registerIcon`.
- Dialogs derive from `frame::StudioDialog` (messages: `frame::MessageDialog`),
  never `QMessageBox`, so they carry the same title bar.
- Top-level windows derive from `frame::StudioWindow`. Its title bar holds the
  app mark, the menus, a centred title and the window buttons. On Windows the
  native frame stays (Aero Snap, shadow, resize borders, maximize animation,
  Windows 11 snap layouts on the maximize button): only the caption is removed
  in `WM_NCCALCSIZE` and `WM_NCHITTEST` maps the bar back to caption, resize and
  maximize hits. Use `menus()` rather than `QMainWindow::menuBar()`.

## Brand

`resources/brand` is the Retrolarium brand set ("Scanline R", see its README):
SVG and pixel-snapped PNG icons, the Windows `.ico`, the macOS `.icns` and the
Linux hicolor tree for packaging. The studio uses the `.ico` for the executable,
the accent tile as the window and taskbar icon in the light theme and the dark
tile in the dark one, and the mark (`mark.svg` on light, `mark-on-dark.svg` on
dark) in the title bar, the welcome page and About; `theme/brand.hpp` hands them
out for the current theme.

## Fonts

`resources/fonts` bundles Barlow, Barlow Condensed and IBM Plex Mono from
[google/fonts](https://github.com/google/fonts) under the SIL Open Font License
1.1 (`OFL-*.txt`).
