# RCPU Address Format

RCPU uses **Bech32 (SegWit) addresses only** on mainnet.

## Bech32 (Supported)

- HRP (Human-Readable Part): `rcpu`
- Address format: `rcpu1...`
- Source: `src/kernel/chainparams.cpp` (`bech32_hrp = "rcpu"`)
- All wallet operations default to Bech32.

## Base58 Legacy (NOT Supported)

The Base58 prefix bytes are inherited from the Bitcoin Core template for
code structure compatibility:

| Type | Prefix byte | Bitcoin meaning |
|------|------------|------------------|
| PUBKEY_ADDRESS | 0 | `1...` (P2PKH) |
| SCRIPT_ADDRESS | 5 | `3...` (P2SH) |
| SECRET_KEY | 128 | WIF private keys |

Source: `src/kernel/chainparams.cpp` (`base58Prefixes`).

**These prefixes are NOT usable on the RCPU mainnet.** The wallet defaults to
Bech32 output type (`OutputType::BECH32`). While the Base58 encoding functions
remain in source for test compatibility, users should not generate or use
`1...` or `3...` addresses.

## Wallet Behavior

- `getnewaddress` returns Bech32 (`rcpu1...`) by default.
- `m_default_address_type` defaults to `OutputType::BECH32`.
- `getnewaddress` and `getrawchangeaddress` **reject** `OutputType::LEGACY` on
  mainnet. The wallet cannot produce Base58 addresses on RCPUMAIN.
- `validateaddress` reports legacy addresses as invalid on mainnet.
- Users and exchanges should only use `rcpu1...` addresses.

## Recommendation for Integrations

- Exchanges, payment processors, and block explorers should validate that
  deposit addresses start with `rcpu1`.
- The `validateaddress` RPC returns the address type; integrations should
  reject non-Bech32 addresses.
- Base58 address rejection is enforced at the wallet RPC layer on mainnet.
