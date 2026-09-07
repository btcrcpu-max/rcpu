# RCPU Consensus Parameters

This is the **single source of truth** for consensus-critical RCPU values.
Other docs reference this table; if a number elsewhere disagrees, **this table and the code win**.

| Parameter | Mainnet value | Source reference |
|-----------|---------------|------------------|
| Chain name (CLI) | `-chain=rcpu` / `-rcpu` | `src/chainparamsbase.cpp` (`ChainType::RCPUMAIN`) |
| Data directory (root) | `~/.rcpu` | `GetDefaultDataDir()` |
| Chain data directory | `~/.rcpu/rcpu/` | `BaseParams().DataDir()` |
| Config file | `~/.rcpu/rcpu.conf` | `BITCOIN_CONF_FILENAME` |
| P2P port | **7227** | `src/kernel/chainparams.cpp` (`nDefaultPort`) |
| RPC port | **7337** | `src/chainparamsbase.cpp` (`CreateBaseChainParams`) |
| Block time | 5 minutes (300 s) | `src/kernel/chainparams.cpp` (`nPowTargetSpacing`) |
| Block reward (start) | 5,000 RCPU | `src/validation.cpp` (`GetBlockSubsidy`) |
| Halving interval | 210,000 blocks | `src/kernel/chainparams.cpp` (`nSubsidyHalvingInterval`) |
| Block subsidy formula | `5000 >> (height / 210000)` | `src/validation.cpp` `GetBlockSubsidy` |
| MAX_MONEY (per-output sanity) | 2,100,000,000 RCPU | `src/consensus/amount.h` |
| **Total subsidy** | **~2,100,000,000 RCPU** | `5000 * 210000 * 2` (geometric series) |
| CT activation height | **0** (genesis) | `src/kernel/chainparams.cpp` (`nCTActivationHeight`) |
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
| nMinimumChainWork | `00000000000000000000000000000000000000000000000000000001fc87aa3c` | `kernel/chainparams.cpp` (chainwork at height 3,600) |
| defaultAssumeValid | `75872099399e9682e72795beeac617f1e911573df97d93a14f6ff0d5adab85d5` | `kernel/chainparams.cpp` (block 3,634 hash) |
| Checkpoints | 0, 1, 2, 5, 10, 20, 30, 38, 3,600 | `kernel/chainparams.cpp` (`checkpointData`) |

## Notes

- `MAX_MONEY` (2.1B) is a **per-output sanity check**, not a total-supply cap.
- Default chain is `RCPUMAIN`; `rcpud` without `-chain` launches the RCPU mainnet.
- `ChainType::MAIN` (Bitcoin mainnet) is **disabled** at runtime; `-chain=main` throws an error.
- The Bitcoin template (`CMainParams`) remains in source for code structure but cannot be instantiated.

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

Tier 2 was applied at **height 3,600** (v1.0.2), corresponding to approximately 3,600 blocks or a target span of ~12.5 days at 5-minute block intervals. The actual wall-clock time was shorter due to faster-than-target block production during early mining. Subsequent hardening tiers are planned:

| Tier | Target height | Approx. age | Action |
|------|---------------|-------------|--------|
| 1 (done) | 38 | ~3 hours | Initial checkpoints, chainwork, assumevalid (v1.0.1) |
| 2 (done) | 3,600 | ~12.5 days (target) | Update nMinimumChainWork, add checkpoint, bump assumevalid (v1.0.2) |
| 3 | 10,000 | ~35 days | Full hardening: chainwork, checkpoints, assumevalid, release v1.1.0 |
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

The tier 2 checkpoint upgrade has been applied at height 3,600 (v1.0.2).
Anti-reorg resistance is now anchored at the tier 2 level.
Subsequent tiers remain as scheduled.
Miners and pools should run with `-reindex-chainstate` if they encounter
unexpected reorgs. Exchanges should require a high number of confirmations
(e.g., 100+) for large deposits during this early period.

