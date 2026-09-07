# RCPU Core Dockerfile - Multi-stage build
# Build: docker build -t rcpu:latest .
# Run:   docker run -d --name rcpu-node -v ~/.rcpu:/home/rcpu/.rcpu -p 7227:7227 -p 127.0.0.1:7337:7337 rcpu:latest

# ===== Stage 1: Build =====
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential libtool autotools-dev automake \
    pkg-config bsdmainutils python3 \
    libevent-dev libboost-dev libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . /build/rcpu

RUN cd /build/rcpu \
    && ./autogen.sh \
    && ./configure --without-gui --disable-bench --disable-tests \
    && make -j$(nproc) \
    && make install

# ===== Stage 2: Runtime =====
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libevent-2.1-7 libboost-system1.74.0 libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash rcpu

USER rcpu
WORKDIR /home/rcpu

VOLUME ["/home/rcpu/.rcpu"]
EXPOSE 7227 7337

COPY --from=builder /usr/local/bin/rcpud /usr/local/bin/rcpud
COPY --from=builder /usr/local/bin/rcpu-cli /usr/local/bin/rcpu-cli

CMD ["rcpud", "-daemon=0", "-printtoconsole"]
