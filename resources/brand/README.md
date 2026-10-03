# Retrolarium — brand assets

Logo: **Scanline R** (variant 1d). The letter R is built from CRT scanlines on a 16 × 16 pixel grid; a terminal cursor sits at the baseline.

## Colors
| Role | Hex |
|---|---|
| Accent (icon tile, cursor on light) | #5980A6 |
| Ink (mark on light) | #1D1F20 |
| Cursor on accent tile | #1D2D3D |
| Dark ground | #17191B |
| Mark on dark | #E4E6E9 |
| Cursor on dark | #94BCE3 |

Wordmark font: **IBM Plex Mono, weight 500**, lowercase `retrolarium` (OFL; ship the TTF and load with `QFontDatabase::addApplicationFont`). The lockup SVGs use live text — convert to outlines before printing or using outside the app.

## Construction
- Grid 16 × 16. Scanlines are 1 unit tall with a 1-unit gap (rows 0, 2, 4 … 14).
- Cursor: x 13–16, y 10–16, solid.
- Icon: mark occupies 62.5 % of the tile, centered. Square corners on Windows/Linux; macOS uses the rounded system shape (824 px rounded rect on a 1024 canvas, radius 185).
- Below 64 px use the provided PNGs — they are pixel-snapped. Do not downscale the SVG for 16/24/32 px.

## Files
- `svg/` — mark (light, dark, mono white), icon (main, dark, macOS), lockups.
- `png/` — icon 16…1024, dark icon, mark 512.
- `windows/retrolarium.ico` (16, 24, 32, 48, 64, 128, 256) + `retrolarium.rc`.
- `macos/retrolarium.icns` (16…1024, rounded from 64 px up).
- `linux/` — hicolor theme tree + `.desktop` file.
- `qt/brand.qrc`, `qt/app_icon.cpp` — resource file and QIcon helper.

## Integration (Qt 5.15)
1. Add `qt/brand.qrc` to the project (paths are relative to `qt/`).
2. Call `app.setWindowIcon(retrolariumAppIcon())` in `main()`.
3. Windows executable icon:
   - qmake: `RC_ICONS = brand/windows/retrolarium.ico`
   - CMake: add `brand/windows/retrolarium.rc` to the target sources.
4. macOS bundle:
   - qmake: `ICON = brand/macos/retrolarium.icns`
   - CMake: `set(MACOSX_BUNDLE_ICON_FILE retrolarium.icns)` and add the file to the bundle `Resources`.
5. Linux: install `linux/hicolor/*` to `$PREFIX/share/icons/hicolor/` and the `.desktop` file to `$PREFIX/share/applications/`.
6. In-app (About dialog, welcome screen): use `:/brand/mark.svg` on light theme, `:/brand/mark-on-dark.svg` on dark.
