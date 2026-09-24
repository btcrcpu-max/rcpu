# RCPU Core Release Notes

RCPU Core release notes live in [doc/release-notes/](release-notes/):

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
