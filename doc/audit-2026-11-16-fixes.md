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

### 4. H-1 — Option B implemented (consensus change)

Files: `src/consensus/params.h`, `src/chain.h`, `src/pow.{h,cpp}`,
`src/node/blockstorage.cpp`, `src/kernel/chainparams.cpp`,
`src/test/dualalgo_tests.cpp`, `contrib/devtools/dual_algo_replay.py`,
`doc/h1-dualalgo-asert-lateness-cap.md`.

`GetDualAlgoWorkRequired()` now re-anchors an algorithm whose last block is
older than `nDualAlgoReanchorGap` (12h mainnet/testnet, 1h regtest) to its
resume block, discarding the banked lateness. The one-time drop is capped at
`nDualAlgoMaxDrop` (8x). Per-algorithm last heights/anchors are stored in
`CBlockIndex` (memory only) and rebuilt in `BlockManager::LoadBlockIndex`,
like `nSecondAlgoCount`. The new rule activates at
`nDualAlgoLatenessFixHeight`; mainnet/testnet default to `INT_MAX` in source
(the release manager must set `TIP + 2016` and announce the hard fork), regtest
is `0`.

New unit test `dualalgo_tests/dual_algo_reanchor_discards_banked_lateness`
covers the 60-day idle case (old rule → powLimit, new rule → 8x, next block
anchored on the resume block). `contrib/devtools/dual_algo_replay.py` compares
old/new targets against a live node's headers before tagging.

**This is a hard fork.** All nodes/pools must upgrade before the activation
height; see the design doc for rollout and test plan.

### 5. H-2 — CT-aware `hash_serialized_4` for assumeutxo

Files: `src/kernel/coinstats.{h,cpp}`, `src/kernel/chainparams.h`,
`src/validation.cpp`, `src/rpc/blockchain.cpp`, `src/test/coins_tests.cpp`.

- New `CoinStatsHashType::HASH_SERIALIZED_CT`: serializes the full `CTxOut`
  (`nValue`, `nNonce`, `vchRangeproof`, script) instead of only the explicit
  amount and script. Exposed as `gettxoutsetinfo` hash type
  `hash_serialized_4` and `kernel::SerializeCoinForHash`.
- `AssumeutxoData` gains optional `hash_serialized_ct`. When set, snapshot
  load and background validation use `hash_serialized_4` and compare against
  it; a CT chain (g_con_elementsmode) with no `hash_serialized_ct` is refused
  rather than falling back to the CT-blind legacy hash.
- The legacy `hash_serialized_3` path is byte-for-byte unchanged.
- Unit test `coins_tests/coinstats_ct_hash_commits_to_ct_fields`: two coins
  differing only in nonce/rangeproof have equal legacy hashes but different
  `hash_serialized_4` hashes.
- RCPU `m_assumeutxo_data` remains empty; when it is populated, the release
  must fill both `blockhash`/`nChainTx` and `hash_serialized_ct` from a synced
  node's `gettxoutsetinfo hash_serialized_4`.

## Validation performed

- `python -c "yaml.safe_load(open('.github/workflows/release-full.yml'))"` —
  YAML parses; all jobs present.
- `git diff --check` — no whitespace errors in the touched files.
- `python -m py_compile contrib/devtools/dual_algo_replay.py` — the replay
  tool compiles.
- The H-1 unit-test expectations were cross-checked with a Python port of
  `CalculateASERT`: old rule on a 60-day gap → `powLimit`; new rule → exactly
  `8x` the last Etchash target; next block ≤ the resume target.
- Manual review of the changed C++ against the call graph:
  `CheckHeadersPoW` → `FindFirstUnknownHeader` → `LookupBlockIndex`;
  `cs_main` is held for the lookup; the suffix height is derived from the last
  known header.

Verified on the S1 build machine (Linux, 4 vCPU, `efcd82c` + this series):

```bash
# build
make -C src -j4 test/test_bitcoin          # success

# named regression tests
./src/test/test_bitcoin --run_test=blockmanager_tests/blockmanager_find_first_unknown_header
./src/test/test_bitcoin --run_test=dualalgo_tests/dual_algo_reanchor_discards_banked_lateness
./src/test/test_bitcoin --run_test=coins_tests/coinstats_ct_hash_commits_to_ct_fields

# broader affected suites
./src/test/test_bitcoin \
  --run_test=dualalgo_tests,blockmanager_tests,coins_tests,pow_tests,tx_verify_tests,confidential_validation_tests
```

Result: all three named tests pass; the broader run is `Running 85 test cases...
*** No errors detected` (exit 0). Two test-only compile errors were found and
fixed during this build: `base_uint<256>::GetCompact` (H-1 test) and
`DataStream`'s `std::byte` value type (H-2 test).

Mainnet replay against a synced S1 node (tip 27,883):

- with `--fix-height 29899` (TIP + 2016 at the time of that replay): 0 old/new
  mismatches, 0 re-anchors. **Historical value:** the height actually shipped in
  `chainparams.cpp` is `29909`. `29899` is the provisional height this replay was
  run with and is kept as a record of what was executed; do not treat it as the
  live parameter.
- with `--fix-height 21000`: exactly 1 historical re-anchor at height 27,104,
  after a 14.36h Etchash gap; the resume target is ~1.23x the last Etchash
  target, far below the 8x cap and not `powLimit`;
- see `RCPU-S1-VERIFICATION-2026-11-16.md` for the full log.

## Follow-up

- Add a functional P2P test once the test framework supports RCPU's 112-byte
  header (`test_framework/messages.py::CBlockHeader` currently serializes only
  80 bytes).
- H-2 is implemented, but no RCPU `m_assumeutxo_data` is populated. When one
  is added, fill `hash_serialized_ct` from `gettxoutsetinfo hash_serialized_4`
  on a synced node; legacy `hash_serialized_3` must not be used for CT chains.
