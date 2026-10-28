# RCPU Core

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](COPYING)
[![Release](https://img.shields.io/github/v/release/btcrcpu-max/rcpu)](https://github.com/btcrcpu-max/rcpu/releases)

RCPU is a CPU-mineable cryptocurrency with **Confidential Transactions (CT)**.
Forked from Bitcoin Core 27.0, replacing SHA-256 PoW with **RandomX** for
ASIC resistance, adding on-chain privacy via Pedersen commitments, and
(mainnet/testnet from block 21,000) an **Etchash light-verification
dual-algo** mode alongside RandomX.

**Repository**: https://github.com/btcrcpu-max/rcpu

> **MANDATORY UPGRADE BEFORE BLOCK 21,000 — v1.3.7 or later.**
> Etchash result comparison is **big-endian** from the first Etchash block.
> v1.3.6 and earlier use the legacy little-endian rule and will reject/split
> at 21,000. Every node, pool and miner that validates or mines bit-30 blocks
> must upgrade; miners need `rcpuminer` 1.0.23+ and pools need the matching
> Miningcore plugin. See [v1.3.7 release notes](doc/release-notes/v1.3.7.md).

| Parameter | Value |
|---|---|
| P2P Port | **7227** |
| RPC Port | **7337** |
| CT Activation | Block **0** (genesis) |
| ASERT Activation | Block **0** (genesis) |
| Dual-Algo Activation | Block **21,000** |
| Etchash Epoch Length | **6000** blocks |
| Etchash Result Byte Order | **big-endian** from block 21,000 |
| Version Bit for Etchash | bit 30 (`0x40000000`) |
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

- **RandomX Proof-of-Work**: CPU-optimized, ASIC/GPU resistant (primary algorithm)
- **Etchash Dual-Algorithm** (height ≥ 21,000): ECIP-1099-style light verification
  on version-bit-30 blocks, in free competition with RandomX (`fDualAlgoEtchash`)
- **Confidential Transactions**: Pedersen commitments from genesis
- **ASERT Difficulty Adjustment**: 2-day half-life, adjusts every block
- **5-minute block time**: 288 blocks per day
- **No pre-mine, no ICO**
- **Subsidy**: 5,000 RCPU per block, halved every 210,000 blocks; after 10 halvings a 1 RCPU tail remains

## Address Format

RCPU defaults to **Bech32 (SegWit)** addresses, prefixed with `rcpu1`.

> Base58 legacy addresses (`1...` / `3...`) are recognized and round-trip on
> the RCPU mainnet since v1.0.23 (encode and decode), matching Bitcoin
> address tooling. However, **wallet RPCs reject `legacy` on mainnet**
> (`getnewaddress` / `getrawchangeaddress`), and WIF private keys stay
> disabled. Do not generate Base58 addresses with the wallet; use `rcpu1...`
> (and `rcpux1...` for confidential receives).

RCPU also supports **confidential addresses** (`rcpux1...`) since v1.0.21.
Sending to a `rcpux1...` address uses Path-B (ECDH) blinding: only the
recipient can unblind the amount. Sending to an ordinary `rcpu1...` address
uses an explicit plaintext (non-confidential) output; the legacy Path-A
(format-only confidentiality) Nonce style is banned above `nBanPathAHeight`
and disabled in the wallet. See
[doc/confidential-transactions.md](doc/confidential-transactions.md) and
[doc/threat-model.md](doc/threat-model.md).

## Quick Start

### Desktop GUI (optional)

```bash
# Install GUI dependencies (Ubuntu 22.04 / 24.04)
sudo apt-get install -y qtbase5-dev qttools5-dev qttools5-dev-tools \
  libqrencode-dev libminiupnpc-dev libminiupnpc17 libprotobuf-dev protobuf-compiler

# Build (RandomX v1.2.1 is a hard dependency of configure.ac)
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 cmake curl ca-certificates patch \
  libevent-dev libboost-dev libsqlite3-dev
sudo bash scripts/build-randomx.sh   # pinned v1.2.1, same as CI/Docker
./autogen.sh
./configure --with-gui=qt5
make -j$(nproc)

# Run
./src/qt/rcpu-qt
```

> **Note:** Official GitHub Releases provide **both** headless binaries
> (Linux/macOS tarball + Windows installer/portable zip) and a GUI AppImage
> (`rcpu-qt`). The GUI can still be built from source for custom
> configurations.
>
> If `libminiupnpc.so` is missing at runtime:
> ```bash
> sudo apt-get install -y libminiupnpc17
> # Or disable UPnP (it is off by default)
> ./src/qt/rcpu-qt -upnp=0
> ```

### Headless Node

```bash
# Build (RandomX v1.2.1 is a hard dependency of configure.ac)
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 cmake curl ca-certificates patch \
  libevent-dev libboost-dev libsqlite3-dev
sudo bash scripts/build-randomx.sh   # pinned v1.2.1, same as CI/Docker
./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check

# Run node (RPC auth via .cookie file; never put a password on the command line)
./src/rcpud -daemon

# Query chain state (rcpu-cli reads the .cookie file automatically)
./src/rcpu-cli getblockchaininfo
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
