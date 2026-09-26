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
| Path A (legacy nonce) ban height | **inactive** (mainnet `INT_MAX`, v1.1.1; testnet: 0; regtest: `INT_MAX`) | `src/kernel/chainparams.cpp` (`nBanPathAHeight`) |
| ASERT activation height | **0** (genesis) | `src/kernel/chainparams.cpp` (`nASERTActivationHeight`) |
| ASERT anchor block | 0 (genesis) | `src/kernel/chainparams.cpp` (`asertAnchorParams`) |
| ASERT half-life | **2 days (172,800 s)** | `src/kernel/chainparams.cpp` (`nASERTHalfLife`) |
| PoW algorithm | RandomX | `src/kernel/chainparams.cpp` (`fPowRandomX`) |
| RandomX epoch | 7 days | `src/kernel/chainparams.cpp` (`nRandomXEpochDuration`) |
| Message start (magic) | `R C P U` (0x52 0x43 0x50 0x55) | `src/kernel/chainparams.cpp` (`pchMessageStart`) |
| Bech32 HRP | `rcpu` (`rcpu1...`) | `src/kernel/chainparams.cpp` (`bech32_hrp`) |
| Base58 prefix (legacy) | 0 / 5 / 128 | `src/kernel/chainparams.cpp` (`base58Prefixes`) |
| Genesis hash | `8f8128ffccc36d188eabd7846dea187d23cba18cbb45cc16d62ec9b8ac2af8e8` | `CreateRcpuGenesisBlock` (nTime=1788566400) |
| Genesis coinbase | `22/Feb/2024 S&P 5087.03 @elonmusk ...` | `kernel/chainparams.cpp` (frozen, do not modify) |
| nMinimumChainWork | `000000000000000000000000000000000000000000000000000000048aa8ea37` | `kernel/chainparams.cpp` (height 5072，2026-09-17 synced node 实测值；v1.1.4 从 height-100 提升) |
| defaultAssumeValid | `8efac7f149d8f2d0b5819b5188d8ecb0d606a2b443a5d0d5d275e78736a12aec` | `kernel/chainparams.cpp` (block 4,922 hash) |
| Checkpoints | 0, 1, 2, 5, 10, 20, 30, 38, 3,600, 4,922 | `kernel/chainparams.cpp` (`checkpointData`) |

## Notes

- There is no hard max supply in consensus code. After 10 halvings the subsidy does not go to zero; it stays at 1 RCPU per block.
- README / marketing "~2.10B" means "sum of the first 10 subsidy eras plus genesis 50", not a cap.
- MAX_MONEY (2.1B) is only a per-output sanity check. A single output above that is invalid; the chain-wide sum can exceed it because of tail emission.
- Default chain is RCPUMAIN; `rcpud` without `-chain` launches the RCPU mainnet.
- `ChainType::MAIN` (Bitcoin mainnet) is disabled at runtime; `-chain=main` throws an error.
- The Bitcoin template (`CMainParams`) remains in source for structure/tests but cannot be selected as the live chain.
- `nMinimumChainWork` is the chainwork at **height 5072**, measured from a real synced node (2026-09-17; raised in v1.1.4, previously a height-100 value from v1.0.8). Raise it only from a synced node's `getblockchaininfo.chainwork` — never by hand from an old document. Every time a hardening tier is advanced, **this table and the `checkpointData` list must be updated in the same change**, or the doc/code drift reappears.

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
| 3 | 10,000 | ~35 days | Read chainwork from a synced node (`getblockchaininfo.chainwork`), raise `nMinimumChainWork`; add checkpoint, bump assumevalid (future release; do not reuse a past version number) |
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

