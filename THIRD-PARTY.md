# Third-party components

Retrolarium Studio itself is under `GPL-3.0-or-later` (`LICENSE`). The
Windows package also carries the components below, each under its own
license. Paths are relative to the installation folder.

| Component | License | Text in the package |
| --- | --- | --- |
| Qt 5.15 (linked statically) | LGPL-3.0 | `licenses/Qt-LGPL-3.0.txt`, with `LICENSE.txt` (GPL-3.0) |
| toml11 (in Retrolarium Core) | MIT | `licenses/toml11-MIT.txt` |
| JSON for Modern C++ (in Retrolarium Core) | MIT | `licenses/nlohmann-json-MIT.txt` |
| Barlow, Barlow Condensed fonts | SIL OFL 1.1 | `licenses/OFL-Barlow.txt` |
| IBM Plex fonts | SIL OFL 1.1 | `licenses/OFL-IBMPlex.txt` |
| Retrolarium Kit | GPL-3.0-or-later | `LICENSE.txt` |
| AS macro assembler (in the kit) | GPL-2.0-or-later | `kit/retrolarium_kit/_toolchains/licenses/AS-GPL-2.0.txt` |
| cc65 (in the kit) | zlib | `kit/retrolarium_kit/_toolchains/licenses/cc65-Zlib.txt` |
| p2bin (in the kit) | ISC | `kit/retrolarium_kit/_toolchains/licenses/p2bin-ISC.txt` |
| Python (private runtime for the kit) | PSF License | `python/LICENSE.txt` |
| MAME | GPL-2.0-or-later | `mame/COPYING` |

The package holds no game ROMs and no console BIOS: the studio downloads
game disassemblies, and the Famicom Disk System BIOS comes from the user's
own console.

Since Qt is linked statically, the LGPL lets you relink the studio with a
modified Qt: the studio's complete source is public under the GPL, and
`README.md` (Build and run) shows how to build it.
