# Retrolarium Studio

Retrolarium Studio changes classic console games from their disassemblies. Open
your own ROM: the studio recognises the game and version, downloads the game's
disassembly, and gives you editors that show each part the way the game draws
it:

- levels and worlds, tiles and blocks, palettes;
- sprites and animations, screens, text;
- music, sound effects and the attract demo.

Build writes a new ROM. Play runs it inside the studio in MAME, from power-on or
right where you are editing, and a running game reloads into each new build.
File → Create IPS patch shares the result.

| Game                                                                                                     | Platform                  |
| -------------------------------------------------------------------------------------------------------- | ------------------------- |
| Super Mario Bros. (Japan/USA, Europe, PlayChoice-10, Disk System, All Night Nippon, Super Mario Bros. 2) | NES / Famicom Disk System |
| Castle Excellent                                                                                         | NES                       |
| Solomon's Key                                                                                            | NES                       |
| Pac-Man                                                                                                  | NES                       |
| Doraemon                                                                                                 | NES                       |
| Flicky                                                                                                   | Mega Drive                |

The interface is in English, Brazilian Portuguese, Spanish, simplified Chinese,
Japanese, French, German, Italian, Polish and Russian.

## Download

Windows 10 or 11 (64-bit). Download the setup program from the Releases page and
run it; it needs no administrator rights. Everything the studio needs comes with
it:

- the Retrolarium Kit, which builds the ROMs, and a private Python that runs it;
- MAME;
- OpenDiskSysROM, a free Famicom Disk System BIOS.

You bring only your ROMs. The studio is not signed yet: the user guide says what
to do when Windows warns about that.

## Documentation

- [User guide](docs/user_guide.md): installing, opening a game, the views, what
  each game offers, troubleshooting.
- [Running from source and packaging](docs/developing.md).
- [Architecture](docs/architecture.md): the plugin model, the source layout, the
  theme rules.
- [Game status](docs/game-status.md): each game's integration in detail.

## Building from source

Requires Docker and a static Qt 5.15 SDK for Windows, cross-compiled with
MinGW-w64, in a Docker image (`tools\build-win.ps1 -Image`). Clone
[retrolarium-core](https://github.com/Retrolarium/retrolarium-core) and
[retrolarium-kit](https://github.com/Retrolarium/retrolarium-kit) beside this
repository: the build compiles the core from there, and the studio runs the kit
for every build and Play.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-win.ps1
powershell -ExecutionPolicy Bypass -File tools\test-win.ps1
powershell -ExecutionPolicy Bypass -File tools\run-win.ps1 -Theme dark
```

The build installs into `build\win-amd64` what the CMake files install: the app,
the helper programs plugins need next to it and the plugins' test programs. A
plugin registers a test with `studio_add_test(target fixture...)` (fixtures
relative to this directory); CTest runs them on a native build and
`test-win.ps1` runs them from the generated `tests.json`.

Open a project directly, optionally at a document:

```powershell
build\win-amd64\retrolarium-studio.exe path\to\flicky-ue.rlj --document levels/round-01
```

A Flicky test project is created with the kit (it only reads the source checkout
and ROM):

```powershell
python ..\retrolarium-kit\rltool.py studio flicky-create "<rom>" --source-root <flicky_src> --project-root build\testdata\flicky
```

Keep project paths short: the source snapshot is deep and Windows path limits
apply.

The studio and the gallery (`--gallery`) can render themselves off screen, which
is how the look is checked without opening a window:

```powershell
build\win-amd64\retrolarium-studio.exe --theme light --size 1600x2700 --screenshot gallery.png
```

`Ctrl+Shift+L` or View → Toggle theme switches the theme at runtime.

## Contributing

Bug reports and pull requests are welcome. A change to a game's editor should
keep a build of the unchanged project byte-identical to the original ROM: the
kit's tests check that. Interface text goes through `tr()`, and new text needs
its translations in every `resources/i18n/*.ts` (`tools/update-translations.ps1`
brings them in).

## License

Retrolarium Studio is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version (`GPL-3.0-or-later`, text in `LICENSE`). It comes without any
warranty.

The Windows package carries other components under their own licenses;
`THIRD-PARTY.md` lists them and where their texts are in the package.
