# Retrolarium Studio: running from source and packaging

This page is for people who build the studio themselves. Players need only the
user guide (`user_guide.md`).

## What a studio from a checkout looks for

The installed studio carries everything next to its program. A studio built from
source finds its parts like this:

- **Retrolarium Kit**, which reads and writes the project and builds the ROM:
  the `RETROLARIUM_KIT` environment variable (`rltool` or `rltool.py`),
  `kit/rltool.py` next to the studio, a `../retrolarium-kit/rltool.py` up to six
  folders above the studio's own folder, then `rltool` on `PATH`. `rltool.py`
  runs with `python/python.exe` next to the studio, else Python on `PATH`.
- **MAME** (`consoles.exe`): `RETROLARIUM_MAME`, the one chosen in File →
  Settings, or `mame/consoles.exe` next to the studio.
- **Game disassemblies**: Open ROM downloads them from the RetrolariumLab
  organisation on GitHub. For a test archive, set `RETROLARIUM_GAMES_SOURCE` to
  a URL or path with `{repository}` in it.

## Building

`tools\build-win.ps1` cross-compiles the studio, its helper programs and the
setup program with the static Qt 5 SDK of
[Longreach Qt](https://github.com/OpenKaryon/longreach-qt) in Docker (the
README's "Building from source" shows how to make its image).
`tools\build-mame-docker.ps1` builds the MAME the package ships the same way:
MinGW-w64 GCC cross-compiles the "consoles" subtarget (NES/Famicom, Vs. System,
SNES and Mega Drive drivers) of an unchanged MAME checkout in Docker, from
`tools\mame\Dockerfile`, and `tools\mame\build.sh` pins the commit.
`tools\build-mame.sh` builds the same subtarget natively in MSYS2.

## The package and the installer

`tools\package-win.ps1` assembles `build\package\RetrolariumStudio`:

- the studio and the kit;
- a private Python taken from the CPython that `python` runs, trimmed and
  isolated;
- MAME with its license texts;
- every license text `THIRD-PARTY.md` lists;
- `BUILD-INFO.txt`, which names the commits the package was made from.

It also writes the folder's zip and `RetrolariumStudio-<version>-setup.exe`: the
installer (`installer/`, in the studio's own frame and style) with that folder
attached and checked file by file. It refuses a working tree with uncommitted
changes, unless `-AllowDirty` is given to try a package out. A missing license
text makes it warn that the package is not ready to publish.

The setup program:

- asks for its language (the studio then starts in it);
- installs for the current user without administrator rights;
- keeps the Disk System BIOS and MAME's settings across updates;
- adds Start menu (and optionally desktop) shortcuts, an Installed Apps entry
  and the `.rlj` association;
- copies itself into the installation as `Uninstall.exe`, which removes all of
  that again, together with the kit's unpacked assemblers. Your games in
  `Documents\Retrolarium` stay unless you ask.
