# Installing RCPU Core

## From release binaries

See [docs/nodes/README.md](docs/nodes/README.md).

Current package name:

```
rcpu-vVERSION-linux-x86_64.tar.gz
```

## From source

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 libevent-dev libboost-dev libsqlite3-dev

./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check
```

- Default data directory: `~/.rcpu`
- Config file: `~/.rcpu/rcpu.conf`
- P2P: **7227**  RPC: **7337**

More detail: [docs/nodes/README.md](docs/nodes/README.md)  
Release process: [doc/release-process.md](doc/release-process.md)  
Consensus parameters: [doc/consensus-params.md](doc/consensus-params.md)
