# Audit 2026-11-16 — patch series

Base: `efcd82c` (v1.4.13). This series contains the low-risk, testable fixes
from `RCPU-AUDIT-2026-11-16.md`; the H-1 consensus change is a design only.

## Changes

### 1. H-4 — skip the known header prefix before PoW verification

Files: `src/node/blockstorage.{h,cpp}`, `src/net_processing.cpp`,
`src/test/blockmanager_tests.cpp`.

`CheckHeadersPoW()` now finds the last header of the batch that is already in
the block index (new `BlockManager::FindFirstUnknownHeader()`) and verifies
only the suffix. A batch of 1,999 public Etchash headers plus one bogus child
therefore costs one HashimotoLight, not 1,999, before the peer is banned. The
per-peer header budget is charged only for the unknown suffix.

The new unit test `blockmanager_tests/blockmanager_find_first_unknown_header`
covers: all known, known prefix + one unknown child, and none known.

### 2. H-3 — fail closed when Windows signing is unavailable

File: `.github/workflows/release-full.yml`.

- A missing `WINDOWS_SIGNING_CERT` now fails the tag release instead of
  printing a warning and publishing unsigned binaries.
- A release with no `*-win64-setup.exe` fails.
- The portable ZIP is unpacked, every `*.exe` is Authenticode-signed,
  verified, and the ZIP is rebuilt; a portable ZIP with no EXE fails.
- `SHA256SUMS-windows.txt` is regenerated after all signing, before the GPG
  signature is produced.
- `osslsigncode`/`unzip` are installed in a separate step that has no signing
  secrets (also addresses M-5 for the apt step).

### 3. M-2 — Linux release RandomX parity gate and PoW suites

File: `.github/workflows/release-full.yml`.

- `build-randomx-linux` now builds `contrib/devtools/rx_parity_gate.cpp`
  against the exact `librandomx.a` that is uploaded and linked, and fails on a
  hash mismatch — the same gate the Windows job already had.
- The Linux release “consensus gate” additionally runs `randomx_tests`,
  `pow_tests` and `dualalgo_tests`, and fails if any suite did not execute.

### 4. H-1 — design only

File: `doc/h1-dualalgo-asert-lateness-cap.md`.

Proposes bounding per-algorithm ASERT lateness (hard cap, or re-anchor on
resume) with a new consensus activation height, unit/functional tests and a
mainnet header replay before tagging. No consensus code is changed in this
series.

## Validation performed

- `python -c "yaml.safe_load(open('.github/workflows/release-full.yml'))"` —
  YAML parses; all jobs present.
- `git diff --check` — no whitespace errors in the touched files.
- Manual review of the changed C++ against the call graph:
  `CheckHeadersPoW` → `FindFirstUnknownHeader` → `LookupBlockIndex`;
  `cs_main` is held for the lookup; the suffix height is derived from the last
  known header.

Not run here (no configured build tree in this workspace):

```bash
# C++ unit test
make -C src test/test_bitcoin
./src/test/test_bitcoin --run_test=blockmanager_tests/blockmanager_find_first_unknown_header

# Full affected suites
./src/test/test_bitcoin --run_test=blockmanager_tests,dualalgo_tests,pow_tests

# Workflow syntax (optional)
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release-full.yml'))"
```

## Follow-up

- Add a functional P2P test once the test framework supports RCPU's 112-byte
  header (`test_framework/messages.py::CBlockHeader` currently serializes only
  80 bytes).
- H-2 (assumeutxo hash) should be fixed before any `m_assumeutxo_data` is
  populated; it is intentionally not part of this series because it needs a
  new hash type and a reindex/snapshot policy.
