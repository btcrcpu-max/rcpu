# RCPU Core Release Notes

RCPU Core release notes live in [doc/release-notes/](release-notes/):

- [v1.3.9](release-notes/v1.3.9.md) — hardening: Etchash headers cost 10× in the per-peer header budget, dual-algo GBT templates are cached per algorithm, plus a fail-loud activation-height guard and a dual-algo reorg functional test. No consensus change.
- [v1.3.8](release-notes/v1.3.8.md) — P2P hardening: skip PoW for known/ancestor header batches (fixes Etchash header replay CPU DoS) plus a per-peer header budget; adds the mainnet-21000 KAT. No consensus change.
- [v1.3.7](release-notes/v1.3.7.md) — **MANDATORY before block 21,000**: Etchash result comparison is big-endian (geth/standard) from the first Etchash block; v1.3.6 and earlier are little-endian and will split/reject. All nodes, pools and miners must upgrade.
- v1.3.6 — superseded by v1.3.7 for Etchash; its binaries use the legacy little-endian rule. Do not use v1.3.6 to mine or validate bit-30 blocks at/after 21,000.
- [v1.3.4](release-notes/v1.3.4.md) — consensus-critical Etchash fix: hashimotoLight now uses the full-DAG dataset size (standard go-ethereum vector); **all Etchash miners/pools must upgrade before height 21,000**
- [v1.3.3](release-notes/v1.3.3.md) — P0 pre-activation fix: Etchash headers are no longer rejected as invalid PoW by the height-unaware pre-checks; upgrade before height 21,000
- [v1.3.0](release-notes/v1.3.0.md) — Etchash dual-algo (ECIP-1099 light verification) on bit-30 blocks from height 21,000 (`fDualAlgoEtchash`); v1.3.1 clean rebuild, v1.3.2 asset filename fix
- [v1.1.7](release-notes/v1.1.7.md) — Wallet default Path C (`0x04`) send path; bare `rcpu1` explicit; 0x04 read-block upgrade (#66)
- [v1.1.6](release-notes/v1.1.6.md) — CI make-check fail-closed, man rename, Windows installer match tighten (#53, #54)
- [v1.1.5](release-notes/v1.1.5.md) — CT send path fix (fail-closed, #51)
- [v1.0.25](release-notes/v1.0.25.md) — Windows IBD high-hash fix (CompressedHeader hashRandomX preservation)
- [v1.0.24](release-notes/v1.0.24.md) — Windows wallet sync fix: RandomX built natively (MSYS2) with chain-hash parity gate
- [v1.0.23](release-notes/v1.0.23.md) — mainnet Base58 address encoding restored; P2TR Path-B unblind fix
- [v1.0.22](release-notes/v1.0.22.md) — `computerandomxhash` returns computed hash in mining mode
- [v1.0.21](release-notes/v1.0.21.md) — confidential addresses (`rcpux1...`); Path-A / Path-B wallet sending
- [v1.0.16](release-notes/v1.0.16.md) — consensus soft fork: B2 reject v2-spend-CT, A2 rangeproof cap
- [v1.0.15](release-notes/v1.0.15.md) — security fixes: blind skip, fee output value, ASERT clamp, RNG strong seed
- [v1.0.14](release-notes/v1.0.14.md) — security: constant-time CKey compare; walletnotify %w unescaped
- [v1.0.12](release-notes/v1.0.12.md) — consensus change: fee outputs excluded from UTXO set
- [v1.0.11](release-notes/v1.0.11.md) — docs / hygiene cleanup, version bump; no consensus changes
- [v1.0.10](release-notes/v1.0.10.md) — build / docs / test / branding cleanup, no consensus changes
- [v1.0.9](release-notes/v1.0.9.md) — consensus change: explicit coinbase outputs (rejects confidential coinbases)

Older `release-notes-*.md` files are retained from the Bitcoin Core 27.0
upstream history for reference and are not RCPU-specific.
