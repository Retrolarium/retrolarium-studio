#!/bin/bash
# Builds the MAME the studio ships: mamedev/mame at the commit below, unchanged,
# as a "consoles" subtarget with only the NES/Famicom, SNES and Mega Drive
# drivers. The result is consoles.exe in the checkout, which
# tools/package-win.ps1 takes (-Mame) together with the checkout's license
# texts (docs/legal, 3rdparty).
#
#   git clone https://github.com/mamedev/mame.git
#   git -C mame checkout c662f9ca1900ed7462dc4fe8d338a98e8e4ce3c3
#   MSYSTEM=UCRT64 /path/to/msys64/usr/bin/bash -lc "/path/to/tools/build-mame.sh /path/to/mame"
#
# It runs in an MSYS2 UCRT64 shell with MAME's build tools installed (see
# MAME's "Compiling MAME" documentation). OS is exported because MAME's
# makefile detects Windows from it, and a login shell started from outside an
# MSYS2 terminal does not inherit it.
set -e

MAME_COMMIT=c662f9ca1900ed7462dc4fe8d338a98e8e4ce3c3
checkout=${1:?usage: build-mame.sh <mame checkout>}

cd "$checkout"
head=$(git rev-parse HEAD)
if [ "$head" != "$MAME_COMMIT" ]; then
    echo "The checkout is at $head; the studio ships $MAME_COMMIT." >&2
    exit 1
fi
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "The checkout has local changes; the studio ships MAME unchanged." >&2
    exit 1
fi

export OS=Windows_NT
exec make \
    SUBTARGET=consoles \
    SOURCES=src/mame/nintendo/nes.cpp,src/mame/nintendo/snes.cpp,src/mame/sega/mdconsole.cpp \
    REGENIE=1 NOWERROR=1 -j"${JOBS:-$(nproc)}"
