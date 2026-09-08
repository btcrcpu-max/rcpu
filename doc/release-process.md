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

1. Tag the release:

   ```bash
   git tag -s v1.0.2 -m "RCPU Core v1.0.2"
   ```

2. Build on a clean Ubuntu 22.04 environment:

   ```bash
   git clean -x -d -f
   ./autogen.sh
   ./configure --without-gui --disable-bench
   make -j$(nproc)
   make check    # must pass on RCPUMAIN
   ```

3. Create the tarball:

   ```bash
   VERSION=1.0.2
   make install DESTDIR="$PWD/stage"
   tar -czf rcpu-v${VERSION}-linux-x86_64.tar.gz \
     -C stage/usr/local/bin rcpud rcpu-cli rcpu-tx rcpu-util rcpu-wallet
   ```

4. Generate checksums:

   ```bash
   sha256sum rcpu-v*-linux-x86_64.tar.gz > SHA256SUMS.txt
   ```

5. Sign the checksums:

   ```bash
   gpg --detach-sign --armor SHA256SUMS.txt
   ```

### Reproducible Builds

RCPU inherits Bitcoin Core's Guix-based reproducible build system from
`contrib/guix`. The Guix configuration is not yet customized for RCPU
binaries. Until Guix is fully adapted:

- Reproducibility is **best-effort** using identical Ubuntu 22.04 environments.
- The same source tree, compiler, and dependency versions will produce
  identical binaries.
- Future releases will integrate Guix for cryptographically verified
  reproducibility.

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
  gpg --verify SHA256SUMS.txt.asc
  sha256sum -c SHA256SUMS.txt
  ```

## Release Checklist

1. [ ] All tests pass (`make check`) on RCPUMAIN
2. [ ] Version bumped in `configure.ac` and `src/clientversion.h`
3. [ ] Git tag created and signed
4. [ ] Binaries built on clean environment
5. [ ] SHA256SUMS generated and signed
6. [ ] GitHub Release created with tarball, SHA256SUMS, and SHA256SUMS.asc
7. [ ] Release notes added to `doc/release-notes/`
8. [ ] `doc/consensus-params.md` matches `chainparams.cpp` (chainwork / assumevalid / checkpoints)
9. [ ] Announcement on Telegram
