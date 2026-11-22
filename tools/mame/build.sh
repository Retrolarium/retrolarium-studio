#!/bin/bash
# Runs inside the image from tools/mame/Dockerfile. /source is the user's MAME
# checkout (read only), /build a volume that keeps a clone and its objects
# between runs, /out where consoles.exe is written.
set -e

# tools/mame/COMMIT, copied into the image (Dockerfile): the one place the
# commit is named, which tools/build-mame.sh and tools/package-win.ps1 read too.
MAME_COMMIT=$(tr -d '[:space:]' </usr/local/share/retrolarium/mame-commit)
SOURCES=src/mame/nintendo/nes.cpp,src/mame/nintendo/vsnes.cpp,src/mame/nintendo/snes.cpp,src/mame/sega/mdconsole.cpp

if [ ! -d /build/mame/.git ]; then
    git clone --no-checkout /source /build/mame
fi
cd /build/mame
git fetch --quiet /source "$MAME_COMMIT"
git checkout --quiet --force "$MAME_COMMIT"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "The build clone has local changes; the studio ships MAME unchanged." >&2
    exit 1
fi

x86_64-w64-mingw32-g++ --version | head -1
make \
    TARGETOS=windows PTR64=1 MINGW64=/usr \
    OVERRIDE_CC=x86_64-w64-mingw32-gcc OVERRIDE_CXX=x86_64-w64-mingw32-g++ \
    OVERRIDE_LD=x86_64-w64-mingw32-ld \
    SUBTARGET=consoles SOURCES="$SOURCES" \
    REGENIE=1 NOWERROR=1 -j"${JOBS:-$(nproc)}"

cp consoles.exe /out/consoles.exe
echo "Built $(git rev-parse HEAD) -> /out/consoles.exe"
