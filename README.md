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
| Genesis nTime | **1788566400** |
| Block Time | 5 minutes |
| Block Reward | 5,000 RCPU |
| Halving Interval | 210,000 blocks |

## Features

- **RandomX Proof-of-Work**: CPU-optimized, ASIC/GPU resistant
- **Confidential Transactions**: Pedersen commitments from genesis
- **ASERT Difficulty Adjustment**: 2-day half-life, adjusts every block
- **5-minute block time**: 288 blocks per day
- **No pre-mine, no ICO**

## Quick Start

```bash
# Build
./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check

# Run node
./src/rcpud -daemon
./src/rcpud -rpcuser=... -rpcpassword=...

# Check chain
./src/rcpud getblockchaininfo
```

## Network

- Explorer: https://explorer.rcpuapp.top
- Mining Pool: https://pool.rcpuapp.top
- Web Wallet: https://wallet.rcpuapp.top
- Telegram: https://t.me/btc_rcpu

## License

MIT License. See [COPYING](COPYING) for details.
Copyright (c) 2009-2021 Bitcoin Core developers
Copyright (c) 2024 The RCPU developers
