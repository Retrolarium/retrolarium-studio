# Changelog

What each Retrolarium Studio release brings. `tools/release.py notes` takes the
section of the version it packages for the release description; add it before
packaging a release.

## Unreleased

- Reject document paths outside the project/game tree, including directory
  links, and recheck actual file paths before saving. Keep Kit-generated
  `.retrolarium/<revision>` projects compatible with sibling `hack/` documents.
- Report Kit identification failures instead of silently treating them as an
  unknown ROM. Update the coordinated Core build pin.
- Include a user README with exact source URLs and build instructions; verify
  the reviewed Qt attribution inventory and all required license texts. Explain
  Qt in About and remove additional application data only when selected.
- Close/reuse embedded emulator views asynchronously, retaining their model and
  session until shutdown. Preserve translations and improve SDK diagnostics.
- Correct the Windows loader configuration and exercise adjacent-DLL policy with
  a real positive control. Run synthetic SMB tests in public CI.

## 0.2.0

This release adds Alien Soldier (Japan and Europe) to the native Studio,
alongside Super Mario Bros., Castle Excellent / Castlequest, Solomon's Key,
Pac-Man, Doraemon and Flicky.

Alien Soldier offers stage maps and objects, tiles, metatiles, palettes, sprite
mappings and animations, screens, text, driver-based sound previews, and its
four attract demos. Edits retain each payload's original capacity. The source
acceptance baseline covers both unchanged ROM builds and the Japanese attract
sequence on a relocated build. Full-game replay and European runtime acceptance
require further checks.
