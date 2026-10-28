# RCPU Consensus Parameters

This is the **single source of truth** for consensus-critical RCPU values.
Other docs reference this table; if a number elsewhere disagrees, **this table and the code win**.

Operational paths (datadir / conf / ports) are not consensus rules.
They are listed here so other docs have one place to copy from.

| Parameter | Mainnet value | Source reference |
|-----------|---------------|------------------|
| Chain name (CLI) | `-chain=rcpu` / `-rcpu` | `src/chainparamsbase.cpp` (`ChainType::RCPUMAIN`) |
| Data directory (root) | `~/.rcpu` | `GetDefaultDataDir()` |
| Chain data directory | `~/.rcpu/rcpu/` | `BaseParams().DataDir()` |
| Config file | `~/.rcpu/rcpu.conf` (basename: `rcpu.conf`) | `src/common/args.cpp` (internal macro still called `BITCOIN_CONF_FILENAME`) |
| P2P port | **7227** | `src/kernel/chainparams.cpp` (`nDefaultPort`) |
| RPC port | **7337** | `src/chainparamsbase.cpp` (`CreateBaseChainParams`) |
| Block time | 5 minutes (300 s) | `src/kernel/chainparams.cpp` (`nPowTargetSpacing`) |
| Block reward (start) | 5,000 RCPU | `src/validation.cpp` (`GetBlockSubsidy`) |
| Halving interval | 210,000 blocks | `src/kernel/chainparams.cpp` (`nSubsidyHalvingInterval`) |
| Block subsidy (height 0) | 50 RCPU | `src/validation.cpp` `GetBlockSubsidy` (genesis special case) |
| Block subsidy (height 1–2,099,999) | `5000 >> (height / 210000)` RCPU | `src/validation.cpp` `GetBlockSubsidy` |
| Block subsidy (height ≥ 2,100,000) | 1 RCPU per block, perpetual | `GetBlockSubsidy`: if (halvings >= 10) return 1 * COIN |
| MAX_MONEY (per-output sanity) | 2,100,000,000 RCPU | `src/consensus/amount.h` — not a total-supply cap |
| Approx. mined by first 10 halvings | ~2.10B RCPU | 50 + 5000 * 210000 * (1 + 1/2 + … + 1/512) |
| Tail emission after height 2,100,000 | ~105,120 RCPU / year | 1 RCPU * 288 blocks/day * 365 |
| CT activation height | **0** (genesis) | `src/kernel/chainparams.cpp` (`nCTActivationHeight`) |
| Path A (legacy nonce) ban height | **12748** (mainnet, v1.1.10); testnet: 0; regtest: `INT_MAX` | `src/kernel/chainparams.cpp` (`nBanPathAHeight`) |
| ASERT activation height | **0** (genesis) | `src/kernel/chainparams.cpp` (`nASERTActivationHeight`) |
| ASERT anchor block | 0 (genesis) | `src/kernel/chainparams.cpp` (`asertAnchorParams`) |
| ASERT half-life | **2 days (172,800 s)** | `src/kernel/chainparams.cpp` (`nASERTHalfLife`) |
| PoW algorithm | RandomX (pre-dual-algo); RandomX + Etchash after height 21000 | `src/kernel/chainparams.cpp` (`fPowRandomX`, `fDualAlgoEtchash`) |
| Dual-algo activation height | **21,000** (mainnet & testnet); **150** (regtest); `INT_MAX` = never (default) | `src/kernel/chainparams.cpp` (`nDualAlgoActivationHeight`) |
| Dual-algo target spacing | 600 s (10 min) per algorithm | `src/kernel/chainparams.cpp` (`nDualAlgoTargetSpacing`) |
| Etchash version bit | **bit 30** (`0x40000000`, `BLOCK_VERSION_QHASH`) | `src/primitives/block.h` |
| Etchash epoch length | **6000** blocks (mainnet/testnet); 10 (regtest) | `src/kernel/chainparams.cpp` (`nEtchashEpochLength`) |
| Etchash result byte order | **big-endian** (geth/ethminer standard) from height 21,000; legacy little-endian before | `src/consensus/params.h` (`nEtchashBigEndianHeight`), `src/pow.cpp` |
| Etchash mode | Light verification only (ECIP-1099 style, no full DAG) | `src/crypto/etchash.cpp`, `src/pow.cpp` (`CheckProofOfWorkEtchash`) |
| RandomX epoch | 7 days | `src/kernel/chainparams.cpp` (`nRandomXEpochDuration`) |
| Message start (magic) | `R C P U` (0x52 0x43 0x50 0x55) | `src/kernel/chainparams.cpp` (`pchMessageStart`) |
| Bech32 HRP | `rcpu` (`rcpu1...`) | `src/kernel/chainparams.cpp` (`bech32_hrp`) |
| Base58 prefix (legacy) | 0 / 5 / 128 | `src/kernel/chainparams.cpp` (`base58Prefixes`) |
| Genesis hash | `8f8128ffccc36d188eabd7846dea187d23cba18cbb45cc16d62ec9b8ac2af8e8` | `CreateRcpuGenesisBlock` (nTime=1788566400) |
| Genesis coinbase | `22/Feb/2024 S&P 5087.03 @elonmusk ...` | `kernel/chainparams.cpp` (frozen, do not modify) |
| nMinimumChainWork | `000000000000000000000000000000000000000000000000000000084efb7f04` | `kernel/chainparams.cpp` (height 10000，2026-09-29 synced node 实测值；v1.1.8) |
| defaultAssumeValid | `d829fb9b1473f6adf98501c613046c42360786b9d5b826ebbb58ad86cd1ba83b` | `kernel/chainparams.cpp` (block 9,992 hash；安全边距 below checkpoint) |
| Checkpoints | 0, 1, 2, 5, 10, 20, 30, 38, 3,600, 4,922, 10,000 | `kernel/chainparams.cpp` (`checkpointData`) |

## Notes

- **Dual-algo (v1.3.0)** is a consensus hard fork at height 21,000: blocks may be mined with either RandomX or a second algorithm selected by version bit 30. On mainnet/testnet `fDualAlgoEtchash = true`, so bit-30 blocks at height ≥ 21,000 are verified with **Etchash** (ECIP-1099-style light verification, epoch = `(height - 21000) / 6000`, 6000 blocks on mainnet/testnet, 10 on regtest); when `fDualAlgoEtchash = false` (pre-etchash builds) the same bit routes to the legacy qhash (qPoW) verifier. Each algorithm runs its own ASERT difficulty targeting a 10-minute spacing, anchored at height 20,999 with a reference target of `nBits >> 1` (twice the pre-fork difficulty); the combined chain keeps ~5-minute cadence. Below the activation height the version bit 30 is ignored and behavior is identical to v1.1.19. All other blocks (bit 30 clear) use RandomX; above the activation height the legacy single-algo ASERT path is not used.
- There is no hard max supply in consensus code. After 10 halvings the subsidy does not go to zero; it stays at 1 RCPU per block.
- README / marketing "~2.10B" means "sum of the first 10 subsidy eras plus genesis 50", not a cap.
- MAX_MONEY (2.1B) is only a per-output sanity check. A single output above that is invalid; the chain-wide sum can exceed it because of tail emission.
- Default chain is RCPUMAIN; `rcpud` without `-chain` launches the RCPU mainnet.
- `ChainType::MAIN` (Bitcoin mainnet) is disabled at runtime; `-chain=main` throws an error.
- The Bitcoin template (`CMainParams`) remains in source for structure/tests but cannot be selected as the live chain.
- `nMinimumChainWork` is the chainwork at **height 10,000** (`000000000000000000000000000000000000000000000000000000084efb7f04`), measured from a real synced node on **2026-09-29** and raised to that value in **v1.1.8** (v1.1.4 used the height-5072 value `0x…` measured 2026-09-17, and v1.0.8 the original height-100 value). Raise it only from a synced node's `getblockchaininfo.chainwork` — never by hand from an old document. Every time a hardening tier is advanced, **this table, this note and the `checkpointData` list must be updated in the same change**, or the doc/code drift reappears.

## Genesis Block

The genesis block nTime is **1788566400** (= 2026-09-05 00:00:00 UTC),
corresponding to the RCPU mainnet launch date.

The genesis coinbase text reads `22/Feb/2024 S&P 5087.03 @elonmusk ...`.
This is a **deliberately retained historical news string** inherited from
the project's early development template. It is **not** the actual block
creation date, nor does it reflect any event on 22 Feb 2024 related to RCPU.

The genesis block hash and coinbase are **frozen on-chain** and cannot be
modified without creating a new chain. Auditors and reviewers should treat
nTime (2026-09-05) as the genesis timestamp and the coinbase text as a
cosmetic artifact, not a factual date.

## Checkpoint and Chainwork Upgrade Schedule

The mainnet was hardened at **height 38** (v1.0.1) with:

- `nMinimumChainWork` set to chainwork at height 38
- `defaultAssumeValid` set to block 30 hash
- Checkpoints at heights: 0, 1, 2, 5, 10, 20, 30, 38

This is the **first hardening tier** (approximately 3 hours of history at 5-minute blocks).

Tier 2 was applied at **height 3,600** (v1.0.2): the tier-2 checkpoint and `defaultAssumeValid` remain at 3,600. The tier-2 **chainwork value was reverted** in v1.0.8 to a measured height-100 value because `0x01fc87aa3c` blocked empty-datadir IBD on new nodes; the chainwork part of tier 2 is therefore **not** live at 3,600 today. In **v1.1.4** the chainwork was re-raised to a height-5,072 measured value and a checkpoint + `defaultAssumeValid` were added at **height 4,922** (tier 2.5), which is the current hardening level. Subsequent hardening tiers are planned:

| Tier | Target height | Approx. age | Action |
|------|---------------|-------------|--------|
| 1 (done) | 38 | ~3 hours | Initial checkpoints, chainwork, assumevalid (v1.0.1) |
| 2 (done) | 3,600 | ~12.5 days (target) | Checkpoint + assumevalid at 3,600 (v1.0.2); nMinimumChainWork reverted to height-100 measured value (v1.0.8) |
| 2.5 (done) | 4,922 | ~17 days | Checkpoint + assumevalid at 4,922, `nMinimumChainWork` raised to height-5,072 measured value (v1.1.4) |
| 3 (done) | 10,000 | ~35 days | Checkpoint + assumevalid at 9,992 / 10,000, `nMinimumChainWork` raised to height-10,000 measured value (v1.1.8) |
| 4 | 100,000 | ~1 year | Long-term hardening, consider removing early checkpoints |

### Upgrade Process

For each tier:

1. **Compute** the chainwork and block hash at the target height using
   `getblockchaininfo` and `getblock <hash>`.
2. **Update** `src/kernel/chainparams.cpp`:
   - `nMinimumChainWork` to the chainwork at the target height
   - `defaultAssumeValid` to the block hash at (target - 8) for safety margin
   - Append the target height to `checkpointData`
3. **Bump version** and tag a new release:
   - Tier 2: patch version (e.g., v1.0.2)
   - Tier 3: minor version (e.g., v1.1.0)
   - Tier 4: minor or major version depending on other changes
4. **Release** signed binaries and SHA256SUMS via GitHub Releases.
5. **Announce** the upgrade via Telegram and the explorer. Nodes should
   upgrade within 2 weeks; old versions remain functional but do not
   benefit from the new chainwork/assumevalid optimizations.

### Warning

The tier 2 checkpoint upgrade has been applied at height 3,600 (v1.0.2):
the checkpoint and `defaultAssumeValid` anchor anti-reorg resistance at
the tier 2 level. The tier 2 `nMinimumChainWork` was reverted in v1.0.8
to the height-100 measured value (see Notes), then re-raised in v1.1.4
to the height-5,072 value with a checkpoint + `defaultAssumeValid` at
height 4,922 (current hardening level, tier 2.5). Future raises must
again come from a synced node's `getblockchaininfo.chainwork`.
Subsequent tiers remain as scheduled.
Miners and pools should run with `-reindex-chainstate` if they encounter
unexpected reorgs. Exchanges should require a high number of confirmations
(e.g., 100+) for large deposits during this early period.

Exchanges: require a high confirmation count (e.g. 100+) while
the chain is below tier 3 (height 10,000).
Checkpoints / nMinimumChainWork reduce IBD risk; they are not a
substitute for confirmations on a young CPU chain.

