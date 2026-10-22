# Retrolarium Studio: game status

What each game's integration covers, in detail. The user guide has the short
version.

## Flicky

Ported: all five workspace documents, the collision codec, the level source
writer (`retrolarium-flicky-level-writer`) and the scene renderer.

The kit's Flicky workspace (`retrolarium_kit.games.flicky.workspace`) makes the
documents from a snapshot of the game's source and stages them back to build;
sound previews render the kit's VGM through `retrolarium-ymfm-renderer`.

| Kind                         | Editor      | State                                                                                                                                   |
| ---------------------------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Levels                       | TileMap     | Objects / Layout / Background layers; place, drag, remove, retype objects; paint collision                                              |
| Tiles                        | Tile        | 8 banks (4bpp and fonts): pencil, fill, pick, preview palette from the round CRAM or any palette                                        |
| Tiles › sega_tilemap         | TileMap     | 12×4 name table, words edited in the inspector, live Enigma size against the 10-byte slot                                               |
| Tiles › sega_palette         | Palette     | 10 compact CRAM entries, slot and 3-bit RGB                                                                                             |
| Palettes                     | Palette     | 28 CRAM palettes, 3-bit RGB picker, rounds drawn with each palette                                                                      |
| Levels › bonus rounds        | TileMap     | the scene simulated at a chosen frame; drag the seesaws, tigers and chick spawns (shared by all 12 bonus rounds)                        |
| Text                         | Table       | 52 fixed-slot strings with length bars, validation and a game-style preview                                                             |
| Sprites › mappings           | Composition | cropped to the sprite and zoomed to fit; pieces drawn with a round's palette; drag a piece, the mirrored `flipped_x` follows            |
| Sprites › animations         | Composition | frame strip at the game's delay, play, reorder frames, delay in the inspector                                                           |
| Sprites › bonus motion       | Table       | raw motion words with their meaning                                                                                                     |
| Sound › music & SFX          | Sequencer   | tracks as channels, drag pitch, Shift-drag length, mute, Listen (kit YMFM render, unsaved edits included), Loop to repeat it until Stop |
| Sound › FM voices, envelopes | Table       | operator rows per voice; the 63 priority and envelope bytes                                                                             |
| Playtest                     | Emulator    | save, build, MAME embedded in the view at the chosen round; pause, stop, screenshot; a card shows build, start and stop progress        |

Build (`Ctrl+B`) saves and runs `rltool studio build` with the plugin's level
writer. Play from selected (`F5`) builds and opens Playtest at the selected
start point: an editor sets it (`EditorModel::startPoint()`, shown as the "Start
point" chip; for Flicky, the round opened in Levels) and the session keeps it
while other documents are open. Play (`Ctrl+F5`) runs the game from power-on
(`rltool studio launch-plan`). Pause (`Pause`) and Stop (`Shift+F5`) sit next to
them and work only while the game runs. MAME is `RETROLARIUM_MAME`, else the one
chosen in Settings, else the one the kit remembers (`rltool movie mame`), else
`mame/consoles.exe` next to the studio, else the nearest
`emulators/mame/mame/consoles.exe` up the tree; the kit is `RETROLARIUM_KIT`,
else the nearest `../retrolarium-kit/rltool.py`, else `rltool` on `PATH`.

MAME draws straight into the Playtest surface (`-attach_window`), so it never
sees its window as focused; the runner passes `-background_input`, without which
game pads are ignored. Its keyboard providers cannot read the keyboard of a
window another process owns, so the surface takes the keys itself while it has
focus (it gets it when the game is up, or on a click) and the runner writes
them, by MAME's names, to a keys file the control script reads each frame; the
script presses every I/O port field whose MAME input assignment names one of
them. Studio shortcuts and menus do not see game keys (Alt and Alt+Space do not
open menus); function keys and Pause stay with the studio. The kit's playtest
script writes a status file when the round is on screen
(`FLICKY_PLAYTEST_RESULT`), which ends the "Starting" card.

Starting fast:

- MAME runs from a temporary folder of the runner: a copy of the cartridge (MAME
  locks the file it runs, so the build can replace the original while the game
  plays) and a control script that MAME runs as its autoboot script, with the
  game's own `-autoboot_script` inside it unchanged.
- When a launch names a ready file (`ProcessSpec::readyFile`), the control
  script runs MAME unthrottled and muted until the file appears, and the
  Playtest view covers the picture, so the boot and the steering to the start
  point pass unseen in a fraction of a second. A launch can instead give a ready
  condition (`ProcessSpec::readyWhen`, a Lua expression with `memory` the main
  CPU's program space); the control script checks it each frame and writes the
  ready file itself after three frames in a row.
- The control script reads `picture.txt` while the game runs: Pixels Square
  scales the 4:3 TV screen MAME assumes back to square pixels (render container
  `xscale`/`yscale`), Colours Studio writes the palette a launch gives
  (`ProcessSpec::palette`, NES: the editors' 64 colours) into the machine's
  palette device. Keys the window keeps (Alt, Ctrl) get aliases: Z and X press
  buttons 1 and 2, Enter start, right Shift select.
- Hot reload: Play while the same game runs (same program, working directory and
  arguments apart from the cartridge and the autoboot script) keeps the process.
  The runner stages a copy of the new build, rewrites the control script with
  the launch's environment and ready file, and leaves a command the script picks
  up within a few frames; it mounts the copy (`image:load`), which hard-resets
  the machine into the new build. MAME reads the autoboot script again on that
  reset. Frame callbacks survive a hard reset in MAME (notifiers and the legacy
  `emu.register_frame_done` list), so the control script routes the game
  script's callbacks through subscriptions it drops on the next run.
- Flicky skips the kit when nothing changed. A fingerprint of the build inputs
  (size and time of every project file outside `build/` and the level writer)
  and the emulator commands planned for that build are kept in
  `build/studio-build.json`, also across sessions. An unchanged project plays in
  about a second; an explicit Build always runs the kit.

Pause suspends MAME. The last frame, taken from the compositor (`PrintWindow`
with `PW_RENDERFULLCONTENT`; a suspended process hands it nothing), is shown in
black and white until Resume. Screenshots use the same capture.

Flicky's font draws lowercase ASCII as kana; the Text inspector says so when a
string has lowercase letters.

Every edit is one step on the project undo stack; Save writes only the changed
documents; Revert returns a document to its state when it was opened (levels:
when the project was created).

## Super Mario Bros.

Projects come from `rldisasm preset <rom>`; every editor reads and writes
`documents/resources.json`, the NES resource bundle. The level renderer is the
original app's, pinned to the kit's output for all 34 SMB1 JU areas by
`smb_level_renderer_golden_test`. `smb_area_streams_test` checks the stream
edits (remove and reinsert every record of every area) against the same scenes;
`tools\test-win.ps1` runs both with the other SMB tests.

| Kind       | Editor      | State                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| ---------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Levels     | TileMap     | areas drawn as the game does (metatiles in the area palette, enemies, the player), day or night, pages of 16 columns; move, add (library) and delete objects and enemies, edit their kind (with previews), size and row and the area header, within the area's stream bytes (SMB1 JU areas are full; with a pooled resource, `capacity_pool`, all levels share the kit's object and enemy ranges, so bytes freed in one level serve another); drag the player, or drop the library's Test start, where Play from selected begins |
| World      | Table       | the route of every world (the area each course step plays, numbered as the game shows levels; a selected step is the Play from selected start) and the nine player physics tables (gravity, jump speeds, top speeds, friction, climbing)                                                                                                                                                                                                                                                                                         |
| Tiles      | Tile        | sprite and background pattern tables, 2bpp, preview palette from the area packets or the player palettes                                                                                                                                                                                                                                                                                                                                                                                                                         |
| Metatiles  | TileMap     | the 16x16 background blocks of each palette group, four patterns each; paint quadrants from the pattern library, take a pattern with the pick tool, type numbers, or copy a whole block                                                                                                                                                                                                                                                                                                                                          |
| Palettes   | Palette     | area packets (eight palettes of four), the packets the game writes over them (snow skies, mushroom platforms, the boss) previewed where they show, and the player table with previews of both players' and the fire colours; the 64-colour NES picker                                                                                                                                                                                                                                                                            |
| Animations | Composition | the player's animations (big and small walk, swim, climb, ...), each its frames of 2x4 sprite patterns in either player's or the fire colours, with the cells the engine flips drawn flipped; set a cell's pattern from the library; World > Player actions picks the frame each action shows                                                                                                                                                                                                                                    |
| Screens    | TileMap     | the status bar, lives, time-up, game-over and warp-zone screens as the PPU display lists leave them; paint tiles from the pattern sheet and 16x16 palettes into the cells the lists write (a repeated write changes its run); on the Writes layer drag a whole write elsewhere or turn it to run down, byte for byte the same list; edit their text fields. With a pooled resource (SMB1 JU) the four game screens share their 110 bytes, so one list may grow while another shrinks                                             |
| Text       | Table       | every fixed text: the fixed fields and the screens' text fields, in the game's font, within each slot; in SMB1 JU the status headings, TIME UP and GAME OVER are as long as typed, up to the end of their row, taking bytes other screen texts give up                                                                                                                                                                                                                                                                           |
| Sound      | Sequencer   | every music header (area music, ground theme parts, events) on the piano roll: square 2, square 1, triangle, noise; drag notes to change pitch, square 1 and drum lengths in place, run lengths and beats in the inspector, which names every song sharing a byte; Play renders the song with the kit (`audio render-smb-wav`); Loop repeats it until Stop                                                                                                                                                                       |
| Playtest   | Emulator    | Play from selected starts at the player's cell (`smb-point-playtest-plan`), Play from power-on                                                                                                                                                                                                                                                                                                                                                                                                                                   |

Play from selected starts hidden and fast: the kit's point script reports
nothing, so the plugin hands the runner a ready condition (`readyWhen`, a Lua
expression over the main CPU's memory: the area running with the player on the
start page) and the control script writes the ready file itself. The swim/stomp
envelope has no editor yet.

MAME shows a "known problems" screen for the NES driver that waits for a key,
which never reaches an embedded MAME. Before a launch the runner marks it as
seen in `cfg/<system>.cfg` (`ui_warnings`, with the features `-listxml`
reports), so MAME's `skip_warnings` setting skips it.

## Castle Excellent

Projects from `rldisasm preset` on Castle Excellent (J) or Castlequest (U).

| Kind     | Editor  | What it does                                                                                                                                                                                                                                                                                             |
| -------- | ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Rooms    | TileMap | 100 rooms of 20x20 cells (16x8 px, two tiles each) coloured by the attribute bytes the loader writes; Cells layer paints from the cell sheet, Objects layer moves, adds and removes static objects (24 per room, 2449-byte stream, 141 bytes of saved state) and the start marker for Play from selected |
| Tiles    | Tile    | both CHR banks, sprite and background tables                                                                                                                                                                                                                                                             |
| Palettes | Palette | room and screen palette tables                                                                                                                                                                                                                                                                           |
| Text     | Table   | the sub-command menu words (A-Z and blank)                                                                                                                                                                                                                                                               |
| Sound FX | Table   | the sound-effect parameter bytes                                                                                                                                                                                                                                                                         |

## Solomon's Key

Projects from `rldisasm preset` on Solomon's Key (USA or Europe).

| Kind            | Editor        | What it does                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| --------------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Rooms           | TileMap       | 53 rooms of 16x12 cells drawn as the game builds them; Blocks paints brown and white blocks, Items moves, adds and removes items (named from the source reconstruction's catalog; hidden or in a brown block) and moves the door, key, mirrors and constellation, Enemies does the same for enemies (named with direction and speed; at most 17) and the player's start; the room inspector sets the tileset, timer speed, head enemies' lifetime, constellation and the ROM layout; budgets count the shared item (1342 B) and enemy (726 B) arenas, or with an Expanded ROM 256 B per room list and 8762 B (Europe 8890 B) for all of them |
| Mirrors         | Table         | the mirror enemy sets (42 B shared, 726 B with an Expanded ROM) and drop schedules, with the rooms that use each                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| Animations      | Composition   | each object's actions grouped into families that show the same frames as drawn or mirrored (the game keeps a frame record per facing, the halves swapped and flipped); drop a pattern pair on a half, flip it or change its palette, and with Mirrors linked the edit reaches every action of the family, mirrored as it shows it; the delay                                                                                                                                                                                                                                                                                                 |
| Screens         | TileMap       | the two packed title layers (logo, castle) on the $2800 name table, painted with tiles $80-$FF; the text screens (room intro, room clear, game over, title text, licence, the three endings) in their font bank: select a line to edit its text, drag it to move it, paint font tiles into it                                                                                                                                                                                                                                                                                                                                                |
| Sound           | Sequencer     | the 26 audio programs by group; notes per virtual channel as the engine steps them, drag to change a note's byte; Listen renders with the kit (`audio render-solomon-wav`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Demo            | Table         | the attract demo's 34 input steps; Play from selected loops the demo in MAME, or records a new one from the controller (`studio solomon-demo-plan`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| Text            | Table         | every line the game draws: HUD, room clear, game over, title, licence, room intro, special-room names and the ending; lines may change length within their regions (`solomon_text`)                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| Tiles, Palettes | Tile, Palette | the four CHR banks; each palette group's colours (a world's four rooms: the shared room template with the group's colour 1), the room intro, title and licence palettes, the ending fade                                                                                                                                                                                                                                                                                                                                                                                                                                                     |

The screen text lives in `documents/text.json` (`retrolarium.solomon-text`).
Projects made before it existed show "Add screen text to this project" under
Text, which runs `rltool studio solomon-upgrade` on the project.

The room inspector's ROM choice Expanded builds the ROM the way Solomon's
Keychain does: the kit (`solomon_expansion`) turns the CNROM image into GxROM
(mapper 66) with two 32 KiB PRG banks. Bank 1 runs the game; bank 0 is a copy
that also holds every room's item and enemy lists, and small wrappers at the
same address in both banks switch to it around the two room decoders. The mirror
enemy sets move into the enemy arena that frees up in bank 1.
