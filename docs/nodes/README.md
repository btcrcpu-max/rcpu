# RCPU Node Deployment Guide

This guide covers installing, configuring, and running a RCPU full node on Linux (Ubuntu/Debian recommended).

## Prerequisites

- Ubuntu 22.04+ or Debian 12+
- Disk/RAM: depends on current chain height and node usage — measure before
  provisioning (see [Resource requirements](#resource-requirements)).

RandomX + CT from genesis is heavier than a plain Bitcoin node.
Prefer more RAM if the node also mines or serves wallets.

## Install

Download the latest release from GitHub Releases:

```bash
VERSION=$(curl -sI https://github.com/btcrcpu-max/rcpu/releases/latest | awk -F'/tag/v' '/^location:/{print $2}' | tr -d '\r')
wget https://github.com/btcrcpu-max/rcpu/releases/download/v${VERSION}/rcpu-${VERSION}-x86_64-linux-gnu.tgz
wget https://github.com/btcrcpu-max/rcpu/releases/download/v${VERSION}/SHA256SUMS-linux.txt
```

Verify:

```bash
# If the release also publishes SHA256SUMS-linux.txt.asc, verify the detached
# signature first (see doc/release-process.md for the signing policy):
#   gpg --verify SHA256SUMS-linux.txt.asc SHA256SUMS-linux.txt
sha256sum -c SHA256SUMS-linux.txt
```

Extract:

```bash
tar -xzf rcpu-${VERSION}-x86_64-linux-gnu.tgz
```

Or build from source:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libtool autotools-dev automake \
  pkg-config bsdmainutils python3 cmake curl ca-certificates patch \
  libevent-dev libboost-dev libsqlite3-dev

git clone https://github.com/btcrcpu-max/rcpu.git
cd rcpu

# RandomX v1.2.1 is a hard dependency of configure.ac; this builds the
# same sha256-verified, patched variant that CI and the Dockerfile use.
sudo bash scripts/build-randomx.sh

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
txindex=0
# RPC auth: with server=1 the node creates a random auth cookie at
# ~/.rcpu/rcpu/.cookie; rcpu-cli and curl can read it directly.
# A static rpcuser/rpcpassword is optional (see cookie docs):
#   rpcuser=your_rpc_user
#   rpcpassword=your_strong_password
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
```

This list mirrors `share/examples/rcpu.conf` and the compiled seeds in
`src/chainparamsseeds.h`; keep the three in sync.

## Monitoring

```bash
ss -tlnp | grep 7337
# Should show: 127.0.0.1:7337

curl -X POST http://127.0.0.1:7337 \
  --user "$(cat ~/.rcpu/rcpu/.cookie)" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"1.0","id":"1","method":"getblockchaininfo","params":[]}'
```

`rcpu-cli` reads the auth cookie automatically (no `-rpcuser`/`-rpcpassword`
needed when the cookie exists) — see [`doc/rcpu-conf.md`](../../doc/rcpu-conf.md).

## Resource requirements

Exact numbers depend on the current chain height and node usage; measure on
the hardware you plan to run before provisioning:

```bash
# Chain data directory size (mainnet datadir layout is ~/.rcpu/rcpu/):
du -sh ~/.rcpu/rcpu

# Resident RAM of a synced, idle rcpud:
ps -o rss= -C rcpud
```

> Documented minimum/recommended disk & RAM figures will be added here once
> measured at the current chain height (numbers are currently pending
> measurement, not filled in).

## Block Subsidy

Genesis pays 50 RCPU; from height 1 the subsidy is 5,000 RCPU per block,
halved every 210,000 blocks. After 10 halvings it stays at 1 RCPU per
block forever — there is no hard max supply in consensus code.

"~2.1B" in the README means the sum of the first 10 subsidy eras plus
the 50 RCPU genesis reward, not a total-supply cap. MAX_MONEY (2.1B)
only limits a single output, not the chain-wide supply.

See [doc/consensus-params.md](../../doc/consensus-params.md) for the
canonical values.

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

