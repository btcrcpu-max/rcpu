# RCPU Release Process

This document describes the process for building, signing, and publishing
RCPU Core releases.

## Versioning

RCPU uses a three-part version number: `vMAJOR.MINOR.PATCH`

- **PATCH**: Bug fixes, doc updates, minor consensus parameter adjustments
  (e.g., new checkpoints)
- **MINOR**: New features, consensus parameter changes that require node
  upgrades
- **MAJOR**: Breaking changes, hard forks

The underlying Bitcoin Core version is tracked separately in `src/clientversion.h`
and `configure.ac`.

## Building Release Binaries

### Prerequisites

- Ubuntu 22.04 LTS (or matching CI environment)
- All build dependencies (see [INSTALL.md](../INSTALL.md))
- GPG key `934D 5BC9 5DD4 B3AC FEF5 21B9 5476 3350 1FE4 B8EE`
  (public key in `RCPU-DEV-GPG-KEY.asc`)

### Build Steps

1. Tag the release (use the current version being released, e.g.
   `v1.0.23`):

```bash
   git tag -s v1.0.23 -m "RCPU Core v1.0.23"
   ```

2. Build on a clean Ubuntu 22.04 environment:

   ```bash
   git clean -x -d -f
   sudo apt-get install -y build-essential libtool autotools-dev automake \
     pkg-config bsdmainutils python3 cmake curl ca-certificates patch \
     libevent-dev libboost-dev libsqlite3-dev
   sudo bash scripts/build-randomx.sh
   ./autogen.sh
   ./configure --without-gui --disable-bench
   make -j$(nproc)
   make check    # must pass on RCPUMAIN
   ```

3. Create the tarball:

   ```bash
   VERSION=1.0.25      # must equal the tag being released
   make install DESTDIR="$PWD/stage"
   tar -czf rcpu-${VERSION}-x86_64-linux-gnu.tgz \
     -C stage/usr/local/bin rcpud rcpu-cli rcpu-tx rcpu-util rcpu-wallet
   ```

4. Generate checksums (same name as the `release-full.yml` CI job):

   ```bash
   sha256sum rcpu-${VERSION}-x86_64-linux-gnu.tgz > SHA256SUMS-linux.txt
   ```

5. Sign the checksums with a detached signature and **upload the resulting
   `.asc` file** — the release workflow publishes `SHA256SUMS-linux.txt` and
   its detached signature `SHA256SUMS-linux.txt.asc`:

   ```bash
   gpg --detach-sign --armor SHA256SUMS-linux.txt
   # produces SHA256SUMS-linux.txt.asc; both files must be uploaded
   ```

### Reproducible Builds

RCPU inherits Bitcoin Core's Guix-based reproducible build system from
`contrib/guix`. The Guix configuration is **not yet customized for RCPU
binaries**; RCPU releases are **not** advertised as reproducible builds. Until
Guix is fully adapted:

- Releases are built on Ubuntu 22.04; built the same way they are expected to
  be reproducible, but this is **not** cryptographically verified.
- Future releases may integrate Guix for cryptographically verified
  reproducibility; this will be announced before being claimed.

### Gitian (Legacy)

Bitcoin Core's Gitian build scripts remain in `contrib/guix` (migrated to Guix).
RCPU does not maintain separate Gitian descriptors. Contributors familiar with
the Bitcoin Core build process can adapt the Guix configuration for RCPU.

## Signing Policy

- **One key, one signer**: Only the key `934D 5BC9 ... 1FE4 B8EE` signs releases.
- **Tags must be signed** (`git tag -s`); commits do not require signatures.
- Tags must be signed with key `934D 5BC9 5DD4 B3AC FEF5 21B9 5476 3350 1FE4 B8EE`.
- The private key is held by the RCPU Dev Team (`rcpudevs@proton.me`).
- The public key is committed to the repository as `RCPU-DEV-GPG-KEY.asc`.
- Verify signatures:

```bash
   gpg --import RCPU-DEV-GPG-KEY.asc
   # Releases publish a detached signature; verify it against the checksums,
   # then verify the checksums against the tarball.
   gpg --verify SHA256SUMS-linux.txt.asc SHA256SUMS-linux.txt
   sha256sum -c SHA256SUMS-linux.txt
   ```

## Release Checklist

1. [ ] All tests pass (`make check`) on RCPUMAIN
2. [ ] Version bumped in `configure.ac` and `src/clientversion.h`
3. [ ] Git tag created and signed
4. [ ] Binaries built on clean environment
5. [ ] SHA256SUMS-linux.txt generated and signed (`SHA256SUMS-linux.txt.asc`)
6. [ ] GitHub Release created with tarball, SHA256SUMS-linux.txt and
      SHA256SUMS-linux.txt.asc
7. [ ] Release notes added to `doc/release-notes/`
8. [ ] `doc/consensus-params.md` matches `chainparams.cpp` (chainwork / assumevalid / checkpoints)
9. [ ] Announcement on Telegram
