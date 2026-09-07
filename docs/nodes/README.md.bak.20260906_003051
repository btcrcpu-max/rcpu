# RCPU Node Deployment Guide

This guide covers installing, configuring, and running a RCPU full node on Linux (Ubuntu/Debian recommended).

## Prerequisites

- Ubuntu 22.04+ or Debian 12+
- 4 GB RAM minimum (8 GB recommended)
- 20 GB free disk space (grows with chain)

## Install

Download the latest release from [GitHub Releases](https://github.com/btcrcpu-max/rcpu/releases):

```bash
wget https://github.com/btcrcpu-max/rcpu/releases/latest/download/rcpu-core-$(VERSION)-linux-x86_64.tar.gz
wget https://github.com/btcrcpu-max/rcpu/releases/latest/download/SHA256SUMS.txt
wget https://github.com/btcrcpu-max/rcpu/releases/latest/download/SHA256SUMS.txt.asc

# Verify
gpg --import RCPU-DEV-GPG-KEY.asc
gpg --verify SHA256SUMS.txt.asc
sha256sum -c SHA256SUMS.txt

# Extract
tar -xzf rcpu-core-*-linux-x86_64.tar.gz
cd rcpu-core-*/bin
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

Create `~/.rcpu/rcpu.conf`:

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

```bash
docker run -d \
  --name rcpu-node \
  -v ~/.rcpu:/root/.rcpu \
  -p 7227:7227 \
  -p 127.0.0.1:7337:7337 \
  rcpu-core:latest
```

## Firewall

```bash
# UFW (Ubuntu)
sudo ufw allow 7227/tcp   # P2P (required)
# DO NOT open 7337 (RPC) to the internet!

# firewalld (CentOS/RHEL)
sudo firewall-cmd --permanent --add-port=7227/tcp
sudo firewall-cmd --reload
```

## Seed Nodes

Add to `rcpu.conf`:

```ini
addnode=seed1.rcpuapp.top:7227
addnode=seed2.rcpuapp.top:7227
```

## Monitoring

```bash
# Check node is running
ss -tlnp | grep 7337
# Should show: 127.0.0.1:7337

# Test RPC
curl -X POST http://127.0.0.1:7337 \
  -u your_rpc_user:your_strong_password \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"1.0","id":"1","method":"getblockchaininfo","params":[]}'
```

## Resources

- Explorer: https://explorer.rcpuapp.top
- Pool: https://pool.rcpuapp.top
- Wallet: https://wallet.rcpuapp.top
- GitHub: https://github.com/btcrcpu-max/rcpu
- Telegram: https://t.me/btc_rcpu
