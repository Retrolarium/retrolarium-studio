# Third-party components

Retrolarium Studio itself is under `GPL-3.0-or-later` (`LICENSE`). The Windows
package also carries the components below, each under its own license. Paths are
relative to the installation folder.

| Component                                                                                                            | License                                                                                | Text in the package                                           |
| -------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| Qt 5.15 (linked statically)                                                                                          | LGPL-3.0                                                                               | `licenses/Qt-LGPL-3.0.txt`, with `LICENSE.txt` (GPL-3.0)      |
| Libraries Qt builds into the studio: zlib, libpng, libjpeg-turbo, PCRE2, FreeType, HarfBuzz, double-conversion, md4c | zlib, libpng, IJG/BSD-3-Clause, BSD-3-Clause, FreeType License, MIT, BSD-3-Clause, MIT | `licenses/qt-3rdparty/`                                       |
| MinGW-w64 runtime and winpthreads (linked statically)                                                                | ZPL-2.1, MIT and others                                                                | `licenses/mingw-w64-runtime.txt`                              |
| toml11 (in Retrolarium Core)                                                                                         | MIT                                                                                    | `licenses/toml11-MIT.txt`                                     |
| JSON for Modern C++ (in Retrolarium Core)                                                                            | MIT                                                                                    | `licenses/nlohmann-json-MIT.txt`                              |
| ymfm (in `tools/retrolarium-ymfm-renderer.exe`)                                                                      | BSD-3-Clause                                                                           | `licenses/ymfm-BSD-3-Clause.txt`                              |
| Barlow, Barlow Condensed fonts                                                                                       | SIL OFL 1.1                                                                            | `licenses/OFL-Barlow.txt`                                     |
| IBM Plex fonts                                                                                                       | SIL OFL 1.1                                                                            | `licenses/OFL-IBMPlex.txt`                                    |
| Retrolarium Kit                                                                                                      | GPL-3.0-or-later                                                                       | `LICENSE.txt`                                                 |
| AS macro assembler (in the kit)                                                                                      | GPL-2.0-or-later                                                                       | `kit/retrolarium_kit/core/toolchains/licenses/AS-GPL-2.0.txt` |
| cc65 (in the kit)                                                                                                    | zlib                                                                                   | `kit/retrolarium_kit/core/toolchains/licenses/cc65-Zlib.txt`  |
| p2bin (in the kit)                                                                                                   | ISC                                                                                    | `kit/retrolarium_kit/core/toolchains/licenses/p2bin-ISC.txt`  |
| OpenDiskSysROM (in the kit)                                                                                          | LGPL-3.0                                                                               | `kit/retrolarium_kit/core/opendisksys/LICENSE-LGPL-3.0.txt`   |
| Python (private runtime for the kit)                                                                                 | PSF License                                                                            | `python/LICENSE.txt`                                          |
| MAME and the libraries built into it                                                                                 | GPL-2.0-or-later; the libraries under their own licenses                               | `mame/COPYING`, `mame/legal/`                                 |

Portions of this software are copyright © The FreeType Project
(https://freetype.org). All rights reserved.

The package holds no game ROMs and no console BIOS dump: the studio downloads
game disassemblies. Famicom Disk System games play on OpenDiskSysROM, a free
re-implementation of the Disk System BIOS. Its source is at
https://github.com/RetrolariumLab/OpenDiskSysROM, and
`kit/retrolarium_kit/core/opendisksys/manifest.json` names the commit the image
was built from. A user may still install their own console's BIOS, which MAME
then prefers.

`mame/consoles.exe` is MAME built unchanged from mamedev/mame commit
`c662f9ca1900ed7462dc4fe8d338a98e8e4ce3c3` (2026-09-08), with only the NES, Vs.
System, SNES and Mega Drive drivers; `tools/build-mame-docker.ps1` (or
`tools/build-mame.sh` in MSYS2) in the studio's source does that build. The Vs.
System palette ROM MAME loads for Vs. Super Mario Bros. is written by the kit
from the RP2C04-0004 colour table the NESdev wiki publishes; it is no dump.
`BUILD-INFO.txt` names this commit and those of the studio, kit and core the
package was made from.

Since Qt is linked statically, the LGPL lets you relink the studio with a
modified Qt: the studio's complete source is public under the GPL, and
`README.md` (Building from source) shows how to build it.
