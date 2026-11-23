# Keep the existing Qt/GCC ABI; replace only its old PE linker.
# The helper supplies a local base reference tied to its inspected ID.
ARG QT_SDK=qt5-win-amd64:latest
FROM ${QT_SDK} AS linker
USER root
RUN mkdir /tmp/binutils && cd /tmp/binutils && \
    wget -q https://sourceware.org/pub/binutils/releases/binutils-2.45.1.tar.xz && \
    echo "5fe101e6fe9d18fdec95962d81ed670fdee5f37e3f48f0bef87bddf862513aa5  binutils-2.45.1.tar.xz" | sha256sum -c - && \
    tar -xf binutils-2.45.1.tar.xz && mkdir build && cd build && \
    ../binutils-2.45.1/configure --target=x86_64-w64-mingw32 \
        --prefix=/opt/retrolarium-binutils --disable-nls --disable-werror \
        --disable-gdb --disable-gprofng --disable-gas --disable-gprof && \
    make -j2 MAKEINFO=true all-ld && make MAKEINFO=true install-ld
FROM ${QT_SDK}
COPY --from=linker /opt/retrolarium-binutils /opt/retrolarium-binutils
USER root
RUN compiler_ld="$(x86_64-w64-mingw32-g++ -print-prog-name=ld)" && \
    compiler_ld="$(command -v "$compiler_ld")" && test -n "$compiler_ld" && \
    ln -sf /opt/retrolarium-binutils/bin/x86_64-w64-mingw32-ld "$compiler_ld" && \
    if [ "$compiler_ld" != /usr/bin/x86_64-w64-mingw32-ld ]; then \
        ln -sf /opt/retrolarium-binutils/bin/x86_64-w64-mingw32-ld /usr/bin/x86_64-w64-mingw32-ld; \
    fi && \
    printf 'int main() { return 0; }\n' > /tmp/linker-probe.cpp && \
    x86_64-w64-mingw32-g++ /tmp/linker-probe.cpp -Wl,--version > /tmp/linker-version.txt && \
    grep -q 'GNU ld.*2\.45\.1' /tmp/linker-version.txt && \
    cat /tmp/linker-version.txt && rm -f /tmp/linker-probe.cpp /tmp/linker-version.txt
