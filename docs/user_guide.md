# Retrolarium Studio: user guide

Retrolarium Studio edits a game's content (levels, graphics, palettes, text,
music and sound effects, demos) and plays the result in MAME. Open your ROM and
the studio finds its game; every game's disassembly also has a `.rlj` file at
the top of its folder that opens it, asking which version of the game to edit.

## Installing

You need Windows 10 or 11 (64-bit), about 250 MB for the studio, and an internet
connection the first time you open each game (the studio downloads its
disassembly then, a few megabytes). Everything else comes with the studio: the
Retrolarium Kit, a private Python that runs it, MAME, and OpenDiskSysROM, a free
BIOS for Famicom Disk System games. You add only your ROMs.

Run `RetrolariumStudio-<version>-setup.exe`. It installs for your Windows user,
without administrator rights, into `%LOCALAPPDATA%\Programs\Retrolarium Studio`.
Running it again updates the studio or repairs it, and Uninstall in Installed
Apps removes it. The zip holds the same folder and runs from wherever you unpack
it, as long as you can write there: MAME keeps its settings in the folder's
`mame` subfolder, so `Program Files` will not do.

The studio is not signed yet, so Windows may stop the setup with "Windows
protected your PC". Choose *More info*, then *Run anyway*. The release page
lists the SHA-256 of each download, so you can check the file first. On a
Windows 11 PC where Smart App Control is on, Windows blocks unsigned programs
outright, and the studio cannot run there until it is signed.

## Where your files are

| What                                                        | Where                                                                                   |
| ----------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| The studio, the kit, Python and MAME                        | `%LOCALAPPDATA%\Programs\Retrolarium Studio` (or where you unpacked the zip)            |
| Downloaded games, your ROM copies and your edits            | `Documents\Retrolarium\<game>`; each version's edits are in its `hack\<version>` folder |
| The kit's assemblers, unpacked on first use                 | `%LOCALAPPDATA%\Retrolarium\toolchains`                                                 |
| MAME's settings and saves                                   | `mame\cfg` and `mame\nvram` in the studio's folder                                      |
| Your Disk System BIOS, if you chose one                     | `mame\roms\fds` in the studio's folder                                                  |
| The studio's own settings (language, MAME, recent projects) | the registry, `HKEY_CURRENT_USER\Software\Retrolarium`                                  |

Updates keep all of these. Uninstalling removes the studio and the unpacked
assemblers. It also removes your games and settings, but only if you tick that
option.

Famicom Disk System games (Super Mario Bros. 2, the Disk System Super Mario
Bros.) play on OpenDiskSysROM. If one misbehaves on it, choose your dump of the
console's BIOS (`disksys.rom`) in File → Settings, and disk games play on that
instead.

Vs. Super Mario Bros. plays on MAME's Vs. System arcade board. The kit writes
each build as the board's ROM chips, so an edited game runs without MAME's
warning about unknown chips, and sets the cabinet to free play: Enter starts a
game, right Shift drops a coin.

Building the studio yourself, or running it from a source checkout, is described
in `developing.md`.

The interface speaks English, Brazilian Portuguese, Spanish, simplified Chinese,
Japanese, French, German, Italian, Polish and Russian. It follows the system's
language until you pick one in File → Settings; the change shows at the next
start. Names from the games themselves (areas, enemies, versions) stay as the
disassembly writes them.

## Opening a game

The simplest way is File → Open ROM… (Ctrl+Shift+O, or the first button on the
start screen): choose your ROM and the studio finds its game and version. The
first time, it downloads the game's disassembly as an archive from the
RetrolariumLab organisation on GitHub (no Git needed) into
`Documents\Retrolarium\<repository>`; later it reuses that folder, or any
disassembly whose `.rlj` you opened before. A ROM the studio does not know (a
hack, a translation, a bad dump) or a game it cannot edit yet is named as such.

A disassembly already on disk opens the same way through its `.rlj`. Open the
game's `.rlj` (for example `super_mario_bros.rlj` at the top of the disassembly)
with File → Open project (Ctrl+O). The studio lists the versions the disassembly
knows (Super Mario Bros.: Japan/USA, Europe, PlayChoice-10, Vs., Disk System,
All Night Nippon, Super Mario Bros. 2) and says which already have their ROM.
Pick one:

- If its ROM is not there yet, the studio asks for it, checks that it is that
  exact version and copies it into the folder. Your file is not changed. A
  version the disassembly builds itself (Pac-Man's expanded NROM-256 layout) is
  built from another version's ROM when you open it; the studio asks for that
  ROM if it is missing.
- The studio then takes the game's data out of the ROM into `hack/<version>/`:
  these are the documents you edit. The original ROM is never written; a build
  lays your edits over it.
- Recent projects remembers the version: reopening it skips the question. File →
  Open another version… asks again.

Each version keeps its own edits. `hack/` and `.retrolarium/` (the studio's
build folder) are not part of the disassembly's repository: they hold game data.
To share a hack, use File → Create IPS patch…: the patch applies to the original
ROM with any IPS patcher. (Flicky keeps its edits in
`.retrolarium/ue/flicky-source`, a copy of the disassembly it builds in.)

A project can also be passed on the command line, optionally with a document:
`retrolarium-studio.exe game.rlj --document levels/round-01`.

A newer studio sometimes edits data an older project lacks (a demo, sound effect
tables, map objects). Opening that view saves your edits, runs the kit's upgrade
for the game and opens the view when it is done; the editor area says
*Preparing…* meanwhile.

## The views

Every game lists the same views, in this order, where the game has them:

| View       | What it edits                                                  |
| ---------- | -------------------------------------------------------------- |
| Levels     | maps, rounds, rooms: paint blocks, move objects and enemies    |
| Tiles      | pattern tables, pen by pen                                     |
| Metatiles  | blocks built from patterns                                     |
| Palettes   | colour tables, previewed where the game uses them              |
| Animations | sprite frames and their sequences                              |
| Screens    | title, logos, guide, ending and credits screens                |
| Text       | the game's strings in its own font                             |
| Sound      | music on a piano roll, then sound effects; Listen plays either |
| Demo       | the attract / title demo: Loop plays it, Record replaces it    |
| Playtest   | the edited build in MAME, from the start or from the selection |

Every edit is one undo step; Save writes the changed documents; Revert returns a
document to how it came from the source.

## Games

| Game              | Notes                                                                                                                                                                                                                                                                                                                                                                                  |
| ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Super Mario Bros. | areas with objects and enemies, world routes and physics (World), music, 18 sound effects (the 1-up, power-up, vine and brick tables are editable: drag their notes), title demo, playtest from any column (Japan/USA, Europe, PlayChoice-10); the Disk System versions play from power-on, and Vs. Super Mario Bros. plays from power-on on MAME's Vs. System board, set to free play |
| Castle Excellent  | rooms with objects, room cells, music track sets and sequences, 5 sound effects with their parameter table, title demo, playtest from any room                                                                                                                                                                                                                                         |
| Solomon's Key     | levels with Spawn Mirrors, music and effects, demo, playtest from any level; USA, Europe and the Japanese Solomon no Kagi                                                                                                                                                                                                                                                              |
| Pac-Man           | the maze, round tables (speed, scatter/chase timing, pellet thresholds, release targets), actors, screens, sound, attract demo, playtest by round; v1.0, v1.1 and the expanded NROM-256 version (a second maze and 8 KiB of sound), which the studio builds from the v1.0 ROM                                                                                                          |
| Flicky            | rounds and bonus rounds (seesaws, tigers, chick spawn), FM voices and envelopes, screens with movable title sprites, attract demos, playtest by round                                                                                                                                                                                                                                  |
| Doraemon          | World 1 city and underground (blocks and objects, hidden ones translucent), World 2 routes (enemy spawns, secret item screens), World 3 underwater rooms (persistent objects and each room's spawn schedule), help and title screens, music for four drivers and 93 sound effects, World 1 demo, playtest from a block, stage or room                                                  |

## Sound effects

NES sound effects are code, not note streams. The kit runs the game's own effect
routines frame by frame and records the sound chip, so the piano roll shows
exactly what each channel plays (one tick is one frame) and Listen renders it.
Notes that come from a data table can be dragged to another pitch the table can
hold; the rest are shown read-only.

## Playtest

Play builds the project and starts it from power-on. *Play from selected* starts
where the open document points (a round, an area column, a room, a block); the
boot and title are skipped and hidden. The game runs in the Playtest view; Pause
and Stop are in the toolbar.

## When something goes wrong

- **"Retrolarium Kit was not found"** or **"The Python that runs the kit was not
  found"**: part of the installation is missing; run the setup again (it repairs
  the studio).
- **"MAME was not found"**: choose `consoles.exe` in File → Settings, or run the
  setup again.
- **Windows stops the setup or the studio**: see Installing above (SmartScreen,
  Smart App Control).
- **"This file is … not …"** when choosing a ROM: it is another version (or a
  changed copy); the message names the version it is.
- **"MAME needs the Famicom Disk System BIOS … OpenDiskSysROM is missing"**: the
  kit lost its copy of the free BIOS; reinstall the studio, or choose your
  `disksys.rom` in File → Settings.
- **"<view> cannot be opened"**: the project has no data for that document and
  the kit could not add it; Messages and Build output show the kit's error.
- **A build fails**: Build output has the kit's log and Messages its error.
