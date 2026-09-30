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
  gallery/   theme gallery: every component on one page, the visual test bench
  app/       entry point
resources/   bundled fonts (OFL) and the Qt resource file
tools/       Docker build and run scripts
```

Planned next, in this order:

```text
src/shell/     main window: global toolbar, kind rail, document list,
               editor host, inspector, bottom panel, status bar
src/sdk/       plugin-facing contracts: profile, data kinds, documents,
               inspector schema, tools, diagnostics (game-agnostic)
src/editors/   the universal editors, first with stub documents
plugins/       platform and game code: nes/, megadrive/, smb1/, flicky/, stub/
```

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

The gallery can render itself off screen, which is how the look is checked
without opening a window:

```powershell
build\win-amd64\retrolarium-studio.exe --theme light --size 1600x2700 --screenshot gallery.png
```

`Ctrl+Shift+L` or View → Toggle theme switches the theme at runtime.

## Fonts

`resources/fonts` bundles Barlow, Barlow Condensed and IBM Plex Mono from
[google/fonts](https://github.com/google/fonts) under the SIL Open Font
License 1.1 (`OFL-*.txt`).
