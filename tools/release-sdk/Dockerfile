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
RUN ln -sf /opt/retrolarium-binutils/bin/x86_64-w64-mingw32-ld /usr/bin/x86_64-w64-mingw32-ld && \
    x86_64-w64-mingw32-ld --version | head -1
