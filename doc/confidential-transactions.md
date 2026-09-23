# Confidential Transactions (CT) on RCPU

This document describes how Confidential Transactions are implemented on the
RCPU mainnet, derived from code in `src/blind.h`, `src/validation.cpp`,
`src/wallet/receive.cpp`, and `src/kernel/chainparams.cpp`.

## Overview

RCPU activates Confidential Transactions (CT) from **genesis (height 0)**.
All non-coinbase transaction outputs use Pedersen commitments to hide amounts,
while coinbase outputs remain in cleartext.

Key source references:

| Component | Source file | Notes |
|-----------|-------------|-------|
| CT activation height | `src/kernel/chainparams.cpp` (`nCTActivationHeight = 0`) | Mainnet and testnet |
| Blinding | `src/blind.h` (`BlindOutput`, `BlindOutputToRecipient`) | Produces commitment + rangeproof |
| Unblinding | `src/blind.h` (`UnblindValue`, `UnblindValueWithKey`) | Recovers amount from commitment |
| Wallet scanning | `src/wallet/receive.cpp` | Wallet scans CT outputs it owns |
| Validation | `src/validation.cpp` (line ~728) | Rejects CT txs before activation height |
| Fee safety | `src/wallet/fees.cpp` | CT safety cap prevents corrupted fee estimates |

## Coinbase and Fees

- **Coinbase outputs are NOT blinded.** Block rewards and fees are paid in
  cleartext, ensuring auditable supply.
- **Coinbase outputs must not be fee-shaped** (explicit value + empty
  `scriptPubKey`). `CheckCoinbaseOutputsExplicit` rejects them so they cannot
  bypass `GetValueOut()` subsidy accounting.
- **Fee outputs** on non-coinbase CT transactions use an explicit value and an
  empty `scriptPubKey`. That script is anyone-can-spend under the interpreter,
  so fee outputs are **never written to the UTXO set** (`CCoinsViewCache::AddCoin`
  skips `IsFee()`). The fee is claimed only via the miner’s coinbase
  (`nFees` + subsidy).
- **Fees are verified** by Pedersen commitment balance (`VerifyAmounts`) plus
  the explicit fee output amount.
- The fee safety cap in `src/wallet/fees.cpp` prevents corrupted fee estimates
  from draining the wallet.

## Range Proofs

- Each blinded output includes a **range proof** (Bulletproofs-style),
  proving the committed amount is in `[0, 2^64)` without revealing the value.
- The rangeproof is stored in `vchRangeproof` on the `CTxOut`.
- Validation rejects outputs with empty rangeproofs when CT is active.

## Wallet CT Output Scanning

The wallet (`src/wallet/receive.cpp`) **automatically scans** its own CT
outputs during block connection and rescan:

1. For each CT output, the wallet attempts `UnblindValue()` using its own
   blinding nonce.
2. If unblinding succeeds, the wallet records the amount and adds the output
   to its balance.
3. If unblinding fails (output belongs to someone else), the output is skipped.

This means:

- Users do **not** need any special configuration to receive CT payments.
- The wallet can scan its own CT outputs without out-of-band communication,
  using the `BlindOutputToRecipient` / `UnblindValueWithKey` flow for
  recipient-key-based unblinding.

## getrawtransaction / Explorer Display

- `getrawtransaction` returns the **confidential value** (Pedersen commitment)
  for blinded outputs, not the plaintext amount.
- Block explorers should display the commitment hash, not a numeric amount.
- The actual amount is only visible to the wallet that owns the output.

## Sending: Path A vs Path B

Since v1.0.21 the wallet supports two blinding paths:

- **Path A (format-only)**: legacy nonce prefix `0x02` written on-chain.
  Anyone can rewind and read the amount. Used when a recipient public key
  cannot be resolved (ordinary `rcpu1...` address without mapped pubkey,
  or `-ctlegacy=1`).
- **Path B (confidential)**: ECDH with the recipient spend pubkey, ephemeral
  key written as `0x03`-prefixed compressed pubkey. Only the recipient can
  unblind. Used for `rcpux1...` confidential addresses and recipients with a
  known pubkey; a missing key on a path-B-required output **fails closed**.

Wallet rule: if `recipient_keys` is non-empty, every non-fee output must have
a pubkey; change outputs use the wallet's own pubkey (path B).

## Confidential addresses (`rcpux1...`)

- Encoding/decoding/validation in `src/key_io.cpp` since v1.0.21; on mainnet
  the prefix is `rcpux1` (regtest: `rrcpux1`).
- The sending wallet persists the original confidential address in `mapValue`
  (`vout_addr`), so history/list shows the human-readable address.
- The on-chain output is an ordinary P2TR script; confidentiality comes from
  ECDH blinding, not from a new script type (consensus unchanged).

## `-ctlegacy` status (mainnet)

`-ctlegacy=1` (wallet-wide path-A fallback) is **usable on mainnet** since
v1.0.18, default 0. With the default, outputs lacking a resolvable recipient
pubkey fall back to path A per-output automatically, so bare `rcpu1...`
sends work without the flag. The path-A spending ban (`nBanPathAHeight`)
is **not active** on mainnet. Path A is not confidential — use it only for
compatibility.
