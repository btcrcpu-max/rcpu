# RCPU Consensus Parameters

This is the **single source of truth** for consensus-critical RCPU values.
Other docs reference this table; if a number elsewhere disagrees, **this table and the code win**.

| Parameter | Mainnet value | Source reference |
|-----------|---------------|------------------|
| Chain name (CLI) | `-chain=rcpu` / `-rcpu` | `src/chainparamsbase.cpp` (`ChainType::RCPUMAIN`) |
| Data directory | `~/.rcpu` | client default |
| Config file | `rcpu.conf` | `src/common/args.cpp` (`BITCOIN_CONF_FILENAME`) |
| P2P port | **7227** | `src/kernel/chainparams.cpp` (`nDefaultPort`) |
| RPC port | **7337** | `src/chainparamsbase.cpp` (`CreateBaseChainParams`) |
| Block time | 5 minutes (300 s) | `src/kernel/chainparams.cpp` (`nPowTargetSpacing`) |
| Block reward (start) | 5,000 RCPU | `src/validation.cpp` (`GetBlockSubsidy`) |
| Halving interval | 210,000 blocks | `src/kernel/chainparams.cpp` (`nSubsidyHalvingInterval`) |
| Block subsidy formula | `5000 >> (height / 210000)` | `src/validation.cpp` `GetBlockSubsidy` |
| MAX_MONEY (per-output sanity) | 2,100,000,000 RCPU | `src/consensus/amount.h` |
| CT activation height | **0** (genesis) | `src/kernel/chainparams.cpp` (`nCTActivationHeight`) |
| ASERT activation height | **0** (genesis) | `src/kernel/chainparams.cpp` (`nASERTActivationHeight`) |
| ASERT anchor block | 0 (genesis) | `src/kernel/chainparams.cpp` (`asertAnchorParams`) |
| ASERT half-life | **2 days (172,800 s)** | `src/kernel/chainparams.cpp` (`nASERTHalfLife`) |
| PoW algorithm | RandomX | `src/kernel/chainparams.cpp` (`fPowRandomX`) |
| RandomX epoch | 7 days | `src/kernel/chainparams.cpp` (`nRandomXEpochDuration`) |
| Message start (magic) | `R C P U` (0x52 0x43 0x50 0x55) | `src/kernel/chainparams.cpp` (`pchMessageStart`) |
| Bech32 HRP | `rcpu` (`rcpu1...`) | `src/kernel/chainparams.cpp` (`bech32_hrp`) |
| Base58 prefix (legacy) | 0 / 5 / 128 | `src/kernel/chainparams.cpp` (`base58Prefixes`) |
| Genesis hash | `8f8128ff...` | `CreateRcpuGenesisBlock` (nTime=1788566400) |

## Notes

- `MAX_MONEY` (2.1B) is a **per-output sanity check**, not a total-supply cap.
- Default chain is `RCPUMAIN`; `rcpud` without `-chain` launches the RCPU mainnet.
