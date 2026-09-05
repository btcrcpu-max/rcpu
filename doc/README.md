RCPU Core
==========

Setup
-----
RCPU Core is the reference client for the RCPU network - a RandomX-proof-of-work
cryptocurrency with Confidential Transactions (CT). It validates blocks and
transactions, and stores the blockchain history.

### Disk usage

The RCPU blockchain is relatively young (launched February 2024) and lightweight.
As of late 2026 the full chain is well under **1 GB** and a full node can run
comfortably on a small VPS with 10 GB of disk space. SSD is recommended but not
required.

Initial sync takes a few minutes to an hour depending on your connection and
hardware - not days.

### Download

Pre-built binaries are available on the GitHub Releases page:

  <https://github.com/RCPUcoin/RCPU/releases>

Verify the SHA256 checksums and GPG signature before running. The signing key is
published in each release as `RCPU-DEV-GPG-KEY.asc`.

Running
-------

### Unix / Linux

Extract the tarball and run:

- `bin/rcpud` (headless daemon) or
- `bin/rcpu-qt` (GUI, if built)

### Windows

Unpack the zip file and run `rcpu-qt.exe` (GUI) or `rcpud.exe` (headless).

### macOS

Drag RCPU Core to your Applications folder, then run it.

### Need Help?

* Read the rest of the documentation in this `doc/` directory.
* Open an issue on GitHub: <https://github.com/RCPUcoin/RCPU/issues>
* Join the community:
  - Telegram: <https://t.me/btc_rcpu>
  - X / Twitter: <https://x.com/btc_rcpu>

Building
--------

Developer build notes for each platform:

- [Dependencies](dependencies.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [macOS Build Notes](build-osx.md)
- [FreeBSD Build Notes](build-freebsd.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)

Development
-----------

The root [README](/README.md) covers project overview, economic parameters, and
contribution guidelines. Network/consensus numbers are maintained in the
single source of truth: [consensus-params.md](consensus-params.md).

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [Fuzz-testing](fuzz-testing.md)

### Upstream

RCPU Core is forked from Bitcoin Core v27. The Bitcoin upstream design docs and
BIP implementations remain relevant for protocol-level understanding:

- [BIPs](bips.md)
- [Benchmarking](benchmarking.md)
- [Internal Design Docs](design/)

### Miscellaneous

- [bitcoin.conf Configuration File](bitcoin-conf.md) *(legacy/upstream — RCPU uses [rcpu.conf](rcpu-conf.md))*
- [CJDNS Support](cjdns.md)
- [I2P Support](i2p.md)
- [Dnsseed Policy](dnsseed-policy.md)
