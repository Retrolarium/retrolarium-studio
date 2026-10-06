# Retrolarium Studio: user guide

Retrolarium Studio edits a game's content (levels, graphics, palettes,
text, music and sound effects, demos) and plays the result in MAME. Every
game's disassembly has a `.rlj` file at the top of its folder; opening it
asks which version of the game to edit and then needs only your copy of
that version's ROM.

## What you need

- **Retrolarium Studio** (`retrolarium-studio.exe`, Windows).
- **Retrolarium Kit**, which reads and writes the project and builds the ROM.
  The studio looks for it in this order: the `RETROLARIUM_KIT` environment
  variable (`rltool` or `rltool.py`), a `retrolarium_kit/rltool.py` up to six
  folders above the studio or the current folder, then `rltool` on `PATH`.
  `rltool.py` needs Python on `PATH`.
- **MAME** (`consoles.exe`) for Play, Playtest, demos and some previews:
  `RETROLARIUM_MAME`, the one chosen in File → Settings, or
  `mame/consoles.exe` next to the studio. Famicom Disk System games also
  need the console's BIOS (`disksys.rom`, yours): choose it in Settings.
- **The game's ROM** (yours) and its disassembly for the games below.

When something is missing, the Messages panel says what and where it looked.

## Opening a game

Open the game's `.rlj` (for example `super_mario_bros.rlj` at the top of the
disassembly) with File → Open project (Ctrl+O). The studio lists the
versions the disassembly knows (Super Mario Bros.: Japan/USA, Europe,
PlayChoice-10, Vs., Disk System, All Night Nippon, Super Mario Bros. 2) and
says which already have their ROM. Pick one:

- If its ROM is not there yet, the studio asks for it, checks that it is
  that exact version and copies it into the folder. Your file is not changed.
- The studio then takes the game's data out of the ROM into
  `hack/<version>/`: these are the documents you edit. The original ROM is
  never written; a build lays your edits over it.
- Recent projects remembers the version: reopening it skips the question.
  File → Open another version… asks again.

Each version keeps its own edits. `hack/` and `.retrolarium/` (the studio's
build folder) are not part of the disassembly's repository: they hold game
data. To share a hack, use File → Create IPS patch…: the patch applies to
the original ROM with any IPS patcher. (Flicky keeps its edits in
`.retrolarium/ue/flicky-source`, a copy of the disassembly it builds in.)

A project can also be passed on the command line, optionally with a
document: `retrolarium-studio.exe game.rlj --document levels/round-01`.

A newer studio sometimes edits data an older project lacks (a demo, sound
effect tables, map objects). Opening that view saves your edits, runs the
kit's upgrade for the game and opens the view when it is done; the editor
area says *Preparing…* meanwhile.

## The views

Every game lists the same views, in this order, where the game has them:

| View | What it edits |
|---|---|
| Levels | maps, rounds, rooms: paint blocks, move objects and enemies |
| Tiles | pattern tables, pen by pen |
| Metatiles | blocks built from patterns |
| Palettes | colour tables, previewed where the game uses them |
| Animations | sprite frames and their sequences |
| Screens | title, logos, guide, ending and credits screens |
| Text | the game's strings in its own font |
| Sound | music on a piano roll, then sound effects; Listen plays either |
| Demo | the attract / title demo: Loop plays it, Record replaces it |
| Playtest | the edited build in MAME, from the start or from the selection |

Every edit is one undo step; Save writes the changed documents; Revert
returns a document to how it came from the source.

## Games

| Game | Notes |
|---|---|
| Super Mario Bros. | areas with objects and enemies, world routes and physics (World), music, 18 sound effects (the 1-up, power-up, vine and brick tables are editable: drag their notes), title demo, playtest from any column (Japan/USA, Europe, PlayChoice-10); the Disk System versions play from power-on with the BIOS, and Vs. Super Mario Bros. needs MAME's arcade driver, which the studio's MAME lacks |
| Castle Excellent | rooms with objects, room cells, music track sets and sequences, 5 sound effects with their parameter table, title demo, playtest from any room |
| Solomon's Key | levels with Spawn Mirrors, music and effects, demo, playtest from any level; USA, Europe and the Japanese Solomon no Kagi |
| Pac-Man | the maze, actors, screens, sound, attract demo, playtest by round |
| Flicky | rounds and bonus rounds (seesaws, tigers, chick spawn), FM voices and envelopes, screens with movable title sprites, attract demos, playtest by round |
| Doraemon | World 1 city and underground (blocks and objects, hidden ones translucent), World 2 routes (enemy spawns, secret item screens), World 3 underwater rooms (persistent objects and each room's spawn schedule), help and title screens, music for four drivers and 93 sound effects, World 1 demo, playtest from a block, stage or room |

## Sound effects

NES sound effects are code, not note streams. The kit runs the game's own
effect routines frame by frame and records the sound chip, so the piano roll
shows exactly what each channel plays (one tick is one frame) and Listen
renders it. Notes that come from a data table can be dragged to another pitch
the table can hold; the rest are shown read-only.

## Playtest

Play builds the project and starts it from power-on. *Play from selected*
starts where the open document points (a round, an area column, a room, a
block); the boot and title are skipped and hidden. The game runs in the
Playtest view; Pause and Stop are in the toolbar.

## When something goes wrong

- **"Retrolarium Kit not found"** or **"MAME was not found"**: set
  `RETROLARIUM_KIT`, or choose MAME in File → Settings (see above).
- **"This file is … not …"** when choosing a ROM: it is another version (or
  a changed copy); the message names the version it is.
- **"MAME needs the Famicom Disk System BIOS"**: choose `disksys.rom` in
  File → Settings.
- **"<view> cannot be opened"**: the project has no data for that document
  and the kit could not add it; Messages and Build output show the kit's
  error.
- **A build fails**: Build output has the kit's log and Messages its error.
