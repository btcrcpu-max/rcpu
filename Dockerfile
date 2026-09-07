# RCPU Core Dockerfile
# Build: docker build -t rcpu:latest .
# Run:   docker run -d --name rcpu-node -v ~/.rcpu:/root/.rcpu -p 7227:7227 -p 127.0.0.1:7337:7337 rcpu:latest

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y     build-essential libtool autotools-dev automake     pkg-config bsdmainutils python3     libevent-dev libboost-dev libsqlite3-dev     && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . /build/rcpu

RUN cd /build/rcpu     && ./autogen.sh     && ./configure --without-gui --disable-bench --disable-tests     && make -j$(nproc)     && make install

RUN useradd -m rcpu && mkdir -p /root/.rcpu
VOLUME ["/root/.rcpu"]

EXPOSE 7227 7337

CMD ["rcpud", "-daemon=0", "-printtoconsole"]
