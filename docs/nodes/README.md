# RCPU Node Deployment Guide

This guide covers installing, configuring, and running a RCPU full node on Linux (Ubuntu/Debian recommended).

## Prerequisites

- Ubuntu 22.04+ or Debian 12+
- 4 GB RAM minimum (8 GB recommended)
- 20 GB free disk space (grows with chain)

RandomX + CT from genesis is heavier than a plain Bitcoin node.
4 GB is the documented minimum; prefer 8 GB if the node also mines
or serves wallets.

## Install

Download the latest release from GitHub Releases:

```bash
VERSION=1.0.2
wget https://github.com/btcrcpu-max/rcpu/releases/download/v${VERSION}/rcpu-v${VERSION}-linux-x86_64.tar.gz
wget https://github.com/btcrcpu-max/rcpu/releases/download/v${VERSION}/SHA256SUMS.txt
wget https://github.com/btcrcpu-max/rcpu/releases/download/v${VERSION}/SHA256SUMS.txt.asc
wget https://raw.githubusercontent.com/btcrcpu-max/rcpu/main/RCPU-DEV-GPG-KEY.asc
```

Verify:

```bash
gpg --import RCPU-DEV-GPG-KEY.asc
gpg --verify SHA256SUMS.txt.asc SHA256SUMS.txt
sha256sum -c SHA256SUMS.txt
```

Extract:

```bash
tar -xzf rcpu-v${VERSION}-linux-x86_64.tar.gz
```

Or build from source:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 libevent-dev libboost-dev libsqlite3-dev

git clone https://github.com/btcrcpu-max/rcpu.git
cd rcpu
./autogen.sh
./configure --without-gui --disable-bench
make -j$(nproc)
make check
```

## Configuration

Create `~/.rcpu/rcpu.conf` (the chain data subdirectory `~/.rcpu/rcpu/` is created automatically):

Do not use bitcoin.conf or ~/.bitcoin.
Canonical paths: see [doc/consensus-params.md](../../doc/consensus-params.md).

```ini
server=1
txindex=1
rpcuser=your_rpc_user
rpcpassword=your_strong_password
rpcbind=127.0.0.1
rpcport=7337
port=7227
```

## Docker

No pre-built image is published. Build first:

```bash
git clone https://github.com/btcrcpu-max/rcpu.git
cd rcpu
docker build -t rcpu:latest .
```

Run:

```bash
docker run -d \
  --name rcpu-node \
  -v ~/.rcpu:/home/rcpu/.rcpu \
  -p 7227:7227 \
  -p 127.0.0.1:7337:7337 \
  rcpu:latest
```

## Firewall

UFW (Ubuntu):

```bash
sudo ufw allow 7227/tcp   # P2P (required)
# DO NOT open 7337 (RPC) to the internet!
```

firewalld (CentOS/RHEL):

```bash
sudo firewall-cmd --permanent --add-port=7227/tcp
sudo firewall-cmd --reload
```

## Seed Nodes

DNS seeds compiled into the binary: seed1.rcpu.top, seed2.rcpu.top.
The IPs below are operator fallbacks if DNS fails; they are not
consensus and may change.

```ini
addnode=seed1.rcpu.top:7227
addnode=seed2.rcpu.top:7227
addnode=38.55.199.177:7227
addnode=119.28.152.245:7227
addnode=207.57.129.188:7227
```

## Monitoring

```bash
ss -tlnp | grep 7337
# Should show: 127.0.0.1:7337

curl -X POST http://127.0.0.1:7337 \
  -u your_rpc_user:your_strong_password \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"1.0","id":"1","method":"getblockchaininfo","params":[]}'
```

## Resources

- Explorer: https://rcpu.ren/
- Pool: https://pool.rcpu.top
- Wallet: https://rcpu.top/
- GitHub: https://github.com/btcrcpu-max/rcpu
- Telegram: https://t.me/btc_rcpu

## Confirmations

Exchanges and services should require a high confirmation count
(e.g. 100+) while the chain is below tier 3 (height 10,000).
See [doc/consensus-params.md](../../doc/consensus-params.md) for the
hardening tier schedule.

