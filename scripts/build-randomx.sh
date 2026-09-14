#!/usr/bin/env bash
# Build and install RandomX v1.2.1 with the RCPU custom PoW configuration.
#
# Pinned to the same version, tarball SHA256 and upstream patch that ci.yml
# and the Dockerfile use. Do not bump any of them independently - update all
# three together.
#
# Usage (from the repository root, bash only):
#   sudo bash scripts/build-randomx.sh      # dev machine (installs to /usr/local)
#   bash scripts/build-randomx.sh           # Docker/CI as root
#
# Requires: curl, ca-certificates, cmake, patch (all in the apt line of the
# "From source" build instructions in README.md / INSTALL.md).
set -euo pipefail

RANDOMX_VERSION="1.2.1"
RANDOMX_SHA256="2e6dd3bed96479332c4c8e4cab2505699ade418a07797f64ee0d4fa394555032"
RANDOMX_URL="https://github.com/tevador/RandomX/archive/refs/tags/v${RANDOMX_VERSION}.tar.gz"
PATCH_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/depends/patches/randomx/custom_configuration.patch"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/build-randomx.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

curl --fail -L -o "${TMP_DIR}/randomx.tar.gz" "${RANDOMX_URL}"
printf '%s  %s\n' "${RANDOMX_SHA256}" "${TMP_DIR}/randomx.tar.gz" | sha256sum -c -
tar -xf "${TMP_DIR}/randomx.tar.gz" -C "${TMP_DIR}"
cd "${TMP_DIR}/RandomX-${RANDOMX_VERSION}"
patch -p1 < "${PATCH_PATH}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build -j"$(nproc)"
cmake --install build
ldconfig

echo "RandomX v${RANDOMX_VERSION} (custom configuration) installed."