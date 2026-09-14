RCPU Core
=========

Intro
-----
RCPU is a CPU-mineable cryptocurrency using RandomX proof-of-work and
Confidential Transactions (CT). This is the Windows full node client.
Users hold the crypto keys to their own money and transact directly with
each other, with the help of a P2P network to check for double-spending.

Setup
-----
Unpack the files into a directory and run rcpud.exe. The node starts on the
RCPU mainnet by default and stores chain data in the default data directory
(see doc/consensus-params.md; override with -datadir=<path>).

Default ports: P2P 7227/tcp, RPC 127.0.0.1:7337.
Do not expose the RPC port (7337) to the internet; keep it bound to
127.0.0.1 and protect it with rpcuser/rpcpassword in the rcpu.conf file.

Official GitHub Releases currently provide the headless binaries (rcpud,
rcpu-cli). The Qt GUI can be built from source where available.

For help and updates see:
  https://rcpu.top/
  https://github.com/btcrcpu-max/rcpu