# Installing RCPU Core

## From release binaries

See [docs/nodes/README.md](docs/nodes/README.md).

Current package name (Linux, see [docs/nodes/README.md](docs/nodes/README.md) for exact asset names per release):

```
rcpu-${VERSION}-x86_64-linux-gnu.tgz
```

## From source

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 cmake curl ca-certificates patch \
  libevent-dev libboost-dev libsqlite3-dev

# RandomX v1.2.1 is a hard dependency of configure.ac; this builds the
# same sha256-verified, patched variant that CI and the Dockerfile use.
sudo bash scripts/build-randomx.sh

./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check
```

`make check` must pass on the default chain RCPUMAIN.

- Default data directory: `~/.rcpu`
- Config file: `~/.rcpu/rcpu.conf`
- P2P: **7227**  RPC: **7337**

More detail: [docs/nodes/README.md](docs/nodes/README.md)  
Release process: [doc/release-process.md](doc/release-process.md)  
Consensus parameters: [doc/consensus-params.md](doc/consensus-params.md)
