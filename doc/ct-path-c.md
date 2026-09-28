# RCPU Path C and Viewing Keys

Status: draft, wallet-layer only
Does not change consensus validation of range proofs or nBanPathAHeight.

## Nonce field (33 bytes)

| byte0 | meaning |
|---|---|
| 0x02 | Path A plaintext nonce (legacy / -ctlegacy=1 only) |
| 0x03 | Path B ephemeral compressed pubkey, CopyX32 KDF |
| 0x04 | Path C ephemeral pubkey X only, HKDF-SHA256 |
| other | invalid, fail closed, no fallback |

Path C layout:
- byte[0] = 0x04
- byte[1..32] = ephemeral pubkey X
- Y parity: store implicitly by trying even-Y first, then odd-Y, when reconstructing
  the secp256k1 point from X. Both attempts must be implemented in test vectors.
  Do not reuse 0x02/0x03 for Path C.

## Path C KDF

ss = ECDH(ephemeral_priv, recipient_spend_pub)
nonce = HKDF-SHA256(
  ikm  = ss,                 // 32-byte x-coordinate, same raw ECDH as Path B
  salt = "rcpu-pathc-v1",
  info = scriptPubKey || 0x01 || le64(64)
)[0:32]

Path B ComputeECDHNonce / CopyX32 must remain byte-identical.
Historical Path B UTXOs stay unwindable only through the old path.

## Viewing key (opt-in)

view_seed = HKDF-SHA256(
  ikm  = ss,
  salt = "rcpu-view-v1",
  info = scriptPubKey
)

- view_seed rewinds amount and optional memo only
- cannot sign
- not embedded in rcpux1
- RPC: getviewingkey / importviewingkey, confirmation required
- default: do not export

## Wallet rules

- destination has spend pubkey (rcpux1) -> Path C
- change -> Path C
- bare rcpu1, no pubkey -> explicit plaintext, never Path A
- recipient_keys non-empty but a non-fee output lacks a key -> fail closed
- unblind dispatch by first byte; unknown prefix -> skip, not amount 0

## Out of scope

Consensus Path A ban, Bulletproofs, Dandelion, Silent Payments.