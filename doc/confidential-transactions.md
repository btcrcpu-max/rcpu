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
- **Fees are computed from the difference** between input commitments and
  output commitments, verified by the consensus layer.
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
