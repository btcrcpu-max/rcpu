# RCPU Address Format

RCPU defaults to **Bech32 (SegWit)** addresses on mainnet, and additionally
supports confidential addresses since v1.0.21.

## Bech32 (Default)

- HRP (Human-Readable Part): `rcpu`
- Address format: `rcpu1...`
- Source: `src/kernel/chainparams.cpp` (`bech32_hrp = "rcpu"`)
- All wallet operations default to Bech32.

## Confidential Addresses (`rcpux1...`)

- Confidential addresses use the `rcpux1` HRP (regtest: `rrcpux1`) and are
  supported since v1.0.21 (encoding/decoding/validation in `src/key_io.cpp`).
- Sending to a `rcpux1...` address uses Path-B (ECDH) blinding: only the
  recipient can unblind the amount. Sending to an ordinary `rcpu1...` address
  falls back to Path-A (format-only confidentiality).
- The on-chain output is an ordinary P2TR script; confidentiality comes from
  ECDH blinding, not from a new script type (consensus unchanged).
- See `doc/confidential-transactions.md` and `doc/threat-model.md` for the
  Path-A / Path-B mechanism.

## Base58 Legacy (compatibility only)

The Base58 prefix bytes are inherited from the Bitcoin Core template for
code structure compatibility:

| Type | Prefix byte | Bitcoin meaning |
|------|------------|------------------|
| PUBKEY_ADDRESS | 0 | `1...` (P2PKH) |
| SCRIPT_ADDRESS | 5 | `3...` (P2SH) |
| SECRET_KEY | 128 | WIF private keys |

Source: `src/kernel/chainparams.cpp` (`base58Prefixes`).

Status on the RCPU mainnet:

- Since **v1.0.23**, Base58 **address** encoding (`1...` / `3...`) is enabled
  for both encode and decode, so addresses round-trip like Bitcoin address
  tooling. Decoding was already allowed before v1.0.23
  (`DecodeDestination` has no mainnet restriction).
- The **wallet RPC layer still rejects `legacy` on mainnet**:
  `getnewaddress` / `getrawchangeaddress` with `legacy` throw
  `RPC_INVALID_PARAMETER`, so the wallet cannot produce Base58 addresses.
- **WIF private keys** (`SECRET_KEY`, prefix 128) remain disabled on mainnet:
  `EncodeSecret` returns empty for mainnet.

## Wallet Behavior

- `getnewaddress` returns Bech32 (`rcpu1...`) by default.
- `m_default_address_type` defaults to `OutputType::BECH32`.
- `getnewaddress` and `getrawchangeaddress` **reject** `OutputType::LEGACY` on
  mainnet. The wallet cannot produce Base58 addresses on RCPUMAIN.
- `validateaddress` behavior follows the code above; refer to
  `src/key_io.cpp` and `src/wallet/rpc/addresses.cpp`.
- Users and exchanges should default to `rcpu1...`, and use `rcpux1...` for
  confidential receives.

## Recommendation for Integrations

- Exchanges, payment processors, and block explorers should validate that
  deposit addresses start with `rcpu1` (or `rcpux1` for confidential
  deposits).
- The `validateaddress` RPC returns the address type; integrations should
  rely on the wallet RPC layer's `legacy` rejection for address generation,
  rather than an address-prefix blacklist.