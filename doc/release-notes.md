# RCPU Core Release Notes

RCPU Core release notes live in [doc/release-notes/](release-notes/):

- [v1.4.6](release-notes/v1.4.6.md) — Security follow-up: v3 ECDSA `SIGHASH_SINGLE` is now rejected too (v1.4.5 only covered Schnorr); release publishing is restricted to signed tag pushes. No consensus parameter change.
- [v1.4.5](release-notes/v1.4.5.md) — Released security release: v3 Taproot SIGHASH_SINGLE is rejected immediately; CT nonce/zero-value rules activate at `nCTStrictRulesHeight` (27,326); wallet plaintext CT amount cache removed; CI/release supply-chain hardening. Superseded by v1.4.6 for the ECDSA H-1 path.
- [v1.4.4](release-notes/v1.4.4.md) — GUI wallet security hardening: new wallets encrypted by default, KDF 1,000,000 rounds, minimum passphrase length, 10-minute idle auto-lock, explicit localhost RPC binding. No consensus change.
- [v1.4.3](release-notes/v1.4.3.md) — Wallet: descriptor-wallet legacy change outputs stay spendable under confidential transactions (they are no longer rewritten to an unspendable `wpkh(key)` script); `wallet_tests` joins the CI unit gate. No consensus change.
- [v1.4.2](release-notes/v1.4.2.md) — post-activation chain anchors at block 21,000; cheaper Etchash header/block verification. No consensus changes.
- [v1.4.1](release-notes/v1.4.1.md) — consensus tightening: confidential inputs must not carry a value, plus RPC/statistics fixes and an index rebuild. Read Compatibility before upgrading.
- [v1.4.0](release-notes/v1.4.0.md) — **MANDATORY before block 21,000**: standard Etchash hash domain (nonce excluded from the header hash); GPU miners can mine RCPU's second algorithm. RandomX and historical blocks unchanged.
- [v1.3.10](release-notes/v1.3.10.md) — P2P sync fix: during IBD, headers are charged 1x instead of the 10x Etchash weight, and an exhausted budget no longer assigns misbehaviour. Fixes the v1.3.9 large-catch-up regression. No consensus change.
- [v1.3.9](release-notes/v1.3.9.md) — hardening: Etchash headers cost 10× in the per-peer header budget, dual-algo GBT templates are cached per algorithm, plus a fail-loud activation-height guard and a dual-algo reorg functional test. No consensus change.
- [v1.3.8](release-notes/v1.3.8.md) — P2P hardening: skip PoW for known/ancestor header batches (fixes Etchash header replay CPU DoS) plus a per-peer header budget; adds the mainnet-21000 KAT. No consensus change.
- [v1.3.7](release-notes/v1.3.7.md) — **MANDATORY before block 21,000**: Etchash result comparison is big-endian (geth/standard) from the first Etchash block; v1.3.6 and earlier are little-endian and will split/reject. All nodes, pools and miners must upgrade.
- v1.3.6 — superseded by v1.3.7 for Etchash; its binaries use the legacy little-endian rule. Do not use v1.3.6 to mine or validate bit-30 blocks at/after 21,000.
- [v1.3.4](release-notes/v1.3.4.md) — consensus-critical Etchash fix: hashimotoLight now uses the full-DAG dataset size (standard go-ethereum vector); **all Etchash miners/pools must upgrade before height 21,000**.
- [v1.3.3](release-notes/v1.3.3.md) — P0 pre-activation fix: Etchash headers are no longer rejected as invalid PoW by the height-unaware pre-checks; upgrade before height 21,000.
- [v1.3.0](release-notes/v1.3.0.md) — Etchash dual-algo (ECIP-1099 light verification) on bit-30 blocks from height 21,000 (`fDualAlgoEtchash`); v1.3.1 clean rebuild, v1.3.2 asset filename fix.
- [v1.1.15](release-notes/v1.1.15.md) — wallet: a CT change output no longer disappears from the balance after the wallet auto-locks (N-02); upgrade if you use a passphrase-encrypted wallet.
- [v1.1.10](release-notes/v1.1.10.md) — Path A (legacy nonce) soft-fork ban scheduled at height 12,748; consensus height frozen at tag time.
- [v1.1.7](release-notes/v1.1.7.md) — Wallet default Path C (`0x04`) send path; bare `rcpu1` explicit; 0x04 read-block upgrade (#66).
- [v1.1.6](release-notes/v1.1.6.md) — CI make-check fail-closed, man rename, Windows installer match tighten (#53, #54).
- [v1.1.5](release-notes/v1.1.5.md) — CT send path fix (fail-closed, #51).
- [v1.0.25](release-notes/v1.0.25.md) — Windows IBD high-hash fix (CompressedHeader hashRandomX preservation).
- [v1.0.24](release-notes/v1.0.24.md) — Windows wallet sync fix: RandomX built natively (MSYS2) with chain-hash parity gate.
- [v1.0.23](release-notes/v1.0.23.md) — mainnet Base58 address encoding restored; P2TR Path-B unblind fix.
- [v1.0.22](release-notes/v1.0.22.md) — `computerandomxhash` returns computed hash in mining mode.
- [v1.0.21](release-notes/v1.0.21.md) — confidential addresses (`rcpux1...`); Path-A / Path-B wallet sending.
- [v1.0.16](release-notes/v1.0.16.md) — consensus soft fork: B2 reject v2-spend-CT, A2 rangeproof cap.
- [v1.0.15](release-notes/v1.0.15.md) — security fixes: blind skip, fee output value, ASERT clamp, RNG strong seed.
- [v1.0.14](release-notes/v1.0.14.md) — security: constant-time CKey compare; walletnotify %w unescaped.
- [v1.0.13](release-notes/v1.0.13.md) — security: constant-time ECDH blinding (H-1) and safe process launch for notify commands (H-2). No consensus change.
- [v1.0.12](release-notes/v1.0.12.md) — consensus change: fee outputs excluded from UTXO set.
- [v1.0.11](release-notes/v1.0.11.md) — docs / hygiene cleanup, version bump; no consensus changes.
- [v1.0.10](release-notes/v1.0.10.md) — build / docs / test / branding cleanup, no consensus changes.
- [v1.0.9](release-notes/v1.0.9.md) — consensus change: explicit coinbase outputs (rejects confidential coinbases).

Older `release-notes-*.md` files are retained from the Bitcoin Core 27.0
upstream history for reference and are not RCPU-specific.
