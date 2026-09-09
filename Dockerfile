# RCPU Core Dockerfile - Multi-stage build
# Build: docker build -t rcpu:latest .
# Run:   docker run -d --name rcpu-node -v ~/.rcpu:/home/rcpu/.rcpu -p 7227:7227 -p 127.0.0.1:7337:7337 rcpu:latest

# ===== Stage 1: Build =====
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential libtool autotools-dev automake \
    pkg-config bsdmainutils python3 \
    curl ca-certificates cmake patch \
    libevent-dev libboost-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . /build/rcpu

# RandomX is a hard dependency: configure.ac aborts when randomx.h /
# librandomx cannot be found. Build the pinned v1.2.1 release the same way
# ci.yml does - sha256-verified tarball, the patched variant from
# depends/patches/randomx (custom PoW configuration), shared CMake build,
# then install system-wide so the RCPU build can link against it.
RUN curl --fail -L -o /tmp/randomx-src.tar.gz \
        https://github.com/tevador/RandomX/archive/refs/tags/v1.2.1.tar.gz \
    && echo "2e6dd3bed96479332c4c8e4cab2505699ade418a07797f64ee0d4fa394555032  /tmp/randomx-src.tar.gz" | sha256sum -c - \
    && tar -xf /tmp/randomx-src.tar.gz -C /tmp \
    && cd /tmp/RandomX-1.2.1 \
    && patch -p1 < /build/rcpu/depends/patches/randomx/custom_configuration.patch \
    && mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    && make -j$(nproc) \
    && make install \
    && ldconfig

RUN cd /build/rcpu \
    && ./autogen.sh \
    && CPPFLAGS="-I/usr/local/include" LDFLAGS="-L/usr/local/lib" \
       ./configure --without-gui --disable-bench --disable-tests \
    && make -j$(nproc) \
    && make install

# ===== Stage 2: Runtime =====
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libevent-2.1-7 libboost-system1.74.0 libsqlite3-0 libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash rcpu

# Shared RandomX library built in Stage 1; ldconfig registers it for rcpud.
COPY --from=builder /usr/local/lib/librandomx.so* /usr/local/lib/
RUN ldconfig

USER rcpu
WORKDIR /home/rcpu

VOLUME ["/home/rcpu/.rcpu"]
EXPOSE 7227 7337

COPY --from=builder /usr/local/bin/rcpud /usr/local/bin/rcpud
COPY --from=builder /usr/local/bin/rcpu-cli /usr/local/bin/rcpu-cli

CMD ["rcpud", "-daemon=0", "-printtoconsole"]