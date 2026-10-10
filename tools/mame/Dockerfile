# The toolchain that builds the MAME the studio ships: Debian's MinGW-w64 GCC
# cross-compiling for 64-bit Windows, with the host compiler and Python that
# MAME's build runs for its own tools. See tools/build-mame-docker.ps1.
FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates git make python3 g++ \
        g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
        binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev \
    && rm -rf /var/lib/apt/lists/*

# MAME needs std::thread, which only the POSIX-threads variant of the MinGW
# compilers provides; MAME's build calls them by their plain names.
RUN update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
    && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix \
    && git config --system --add safe.directory '*'

COPY build.sh /usr/local/bin/build-mame
ENTRYPOINT ["bash", "/usr/local/bin/build-mame"]
