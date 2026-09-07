# Building RCPU Core

## Linux (Ubuntu/Debian)

### Install dependencies

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake   pkg-config bsdmainutils python3 libevent-dev libboost-dev libsqlite3-dev
```

### Build from source

```bash
git clone https://github.com/btcrcpu-max/rcpu.git
cd rcpu
./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check    # verify tests pass on RCPUMAIN chain
```

The binaries are in `src/`:
- `src/rcpud` -- the RCPU daemon
- `src/rcpu-cli` -- the command-line RPC client

### Run a node

```bash
./src/rcpud -daemon -rpcuser=rcpurpc -rpcpassword=YOUR_PASSWORD
./src/rcpu-cli -rpcuser=rcpurpc -rpcpassword=YOUR_PASSWORD getblockchaininfo
```

For full deployment instructions, see the
[Node Deployment Guide](docs/nodes/README.md).

For platform-specific build instructions, see [doc/build-unix.md](doc/build-unix.md).
