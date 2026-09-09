# RCPU Core

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](COPYING)
[![Release](https://img.shields.io/github/v/release/btcrcpu-max/rcpu)](https://github.com/btcrcpu-max/rcpu/releases)

RCPU is a CPU-mineable cryptocurrency with **Confidential Transactions (CT)**.
Forked from Bitcoin Core 27.0, replacing SHA-256 PoW with **RandomX** for
ASIC resistance, and adding on-chain privacy via Pedersen commitments.

**Repository**: https://github.com/btcrcpu-max/rcpu

| Parameter | Value |
|---|---|
| P2P Port | **7227** |
| RPC Port | **7337** |
| CT Activation | Block **0** (genesis) |
| ASERT Activation | Block **0** (genesis) |
| Genesis nTime | **1788566400** (2026-09-05 UTC) |
| Block Time | 5 minutes |
| Halving Interval | 210,000 blocks |
| Block subsidy | 5,000 RCPU from height 1; genesis is 50 RCPU |
| After 10 halvings | 1 RCPU per block (tail emission, no hard cap) |
| Approx. subsidy by height 2,100,000 | ~2.10B RCPU (then +105,120 RCPU/year) |

Canonical values: [doc/consensus-params.md](doc/consensus-params.md).
If this README disagrees with that file or the code, the code wins.

## Genesis Block

Treat genesis nTime = **1788566400** (2026-09-05 UTC) as the launch
timestamp. The coinbase news string is a frozen development artifact
and is not the block date. Do not change either value.

## Features

- **RandomX Proof-of-Work**: CPU-optimized, ASIC/GPU resistant
- **Confidential Transactions**: Pedersen commitments from genesis
- **ASERT Difficulty Adjustment**: 2-day half-life, adjusts every block
- **5-minute block time**: 288 blocks per day
- **No pre-mine, no ICO**
- **Subsidy**: 5,000 RCPU per block, halved every 210,000 blocks; after 10 halvings a 1 RCPU tail remains

## Address Format

RCPU uses **Bech32 (SegWit)** addresses only, prefixed with `rcpu1`.

> **Do not use Base58 legacy addresses.** The base58 prefix bytes (0/5/128)
> are inherited from the Bitcoin template for code compatibility but are
> **not supported on the RCPU mainnet**. Only `rcpu1...` addresses are valid.

## Quick Start

### Desktop GUI (optional)

```bash
# Install GUI dependencies (Ubuntu 22.04 / 24.04)
sudo apt-get install -y qtbase5-dev qttools5-dev qttools5-dev-tools \
  libqrencode-dev libminiupnpc-dev libminiupnpc17 libprotobuf-dev protobuf-compiler

# Build
./autogen.sh
./configure --with-gui=qt5
make -j$(nproc)

# Run (binary name depends on build: rcpu-qt or bitcoin-qt)
./src/qt/rcpu-qt 2>/dev/null || ./src/qt/bitcoin-qt
```

> **Note:** Official GitHub Releases currently provide headless binaries only.
> GUI must be built from source or wait for automated packaging (AppImage).
>
> If `libminiupnpc.so` is missing at runtime:
> ```bash
> sudo apt-get install -y libminiupnpc17
> # Or disable UPnP
> ./src/qt/rcpu-qt -upnp=0
> ```

### Headless Node

```bash
# Build
./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check

# Run node
./src/rcpud -daemon -rpcuser=rcpurpc -rpcpassword=YOUR_PASSWORD

# Query chain state
./src/rcpu-cli -rpcuser=rcpurpc -rpcpassword=YOUR_PASSWORD getblockchaininfo
```

## Network

- Explorer: https://rcpu.ren/
- Mining Pool: https://pool.rcpu.top
- Web Wallet: https://rcpu.top/
- Telegram: https://t.me/btc_rcpu

Telegram group name still uses the historical btc_rcpu handle.

## License

MIT License. See [COPYING](COPYING) for details.
Copyright (c) 2009-2024 Bitcoin Core developers
Copyright (c) 2024-2026 The RCPU developers
