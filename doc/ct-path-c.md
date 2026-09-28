# RCPU Path C and Viewing Keys

Status: draft, wallet-layer only
Does not change consensus validation of range proofs or nBanPathAHeight.

## Normative statements

- KDF is RFC 5869 HKDF-SHA256, L = 32.
- String salts/info are UTF-8 raw bytes, no NUL terminator (e.g. `"rcpu-pathc-v1"`).
- All hashing is single-block SHA256 unless stated otherwise.

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
  Do not reuse 0x02/0x03 for Path C. `0x04` is not a valid compressed pubkey
  prefix, so there is no ambiguity with Path A/B serializations.

## Path C KDF

ss = ECDH(ephemeral_priv, recipient_spend_pub)
nonce = HKDF-SHA256(
  ikm  = ss,                 // 32-byte x-coordinate, same raw ECDH as Path B
  salt = "rcpu-pathc-v1",
  info = scriptPubKey || 0x01 || le64(nBits=64)
)[0:32]

ECDH definition (frozen):
- Path C `ss` reuses the existing `secp256k1_ecdh(ctx, out, &pub, seckey, CopyX32, nullptr)`
  call: output is the raw 32-byte x-coordinate, no SHA256, no HKDF pre-hash.
- Path B `ComputeECDHNonce` / `CopyX32` must remain byte-identical.
- Historical Path B UTXOs stay unwindable only through the old path.
- `info` binds the raw scriptPubKey bytes of the output (e.g. P2TR is
  `0x51 0x20 || <32-byte x-only>`); fee outputs (empty scriptPubKey) never carry
  a Path C nonce. Binding the exact script bytes means a different address type
  yields a different nonce even for the same recipient key.

## Point recovery (0x04 || X)

- Try `0x02 || X` (even Y) first; on parse failure try `0x03 || X` (odd Y).
- If both fail, the output is not decryptable by this key: skip, do not treat
  as amount 0, do not fall back to Path A/B.

## Per-output ephemeral

- Each CT output gets an independent ephemeral key.
- Never reuse an ephemeral across outputs or transactions, even for the same
  scriptPubKey appearing twice in one tx.

## Fee / explicit outputs

- Fee outputs and bare-`rcpu1` explicit outputs never carry a Path C nonce.

## -ctlegacy interaction

- `-ctlegacy=1` forces the whole transaction onto the legacy path (Path A only);
  Path C is never mixed with Path A in the same non-fee output set.
- Default sending forbids Path A (no "looks confidential" fallback).

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
- Authorization is per-wallet or per-output; never ship a view key alongside the
  receiving address.

### Rewind consistency (frozen)

A view key holder must be able to rewind the same outputs a spend key can.
The sender derives the nonce as:

    nonce = HKDF-SHA256(ikm = ss, salt = "rcpu-pathc-v1",
                        info = scriptPubKey || 0x01 || le64(64))[0:32]

A view-key rewind MUST produce the identical nonce via:

    nonce = HKDF-SHA256(ikm = view_seed, salt = "rcpu-rewind-v1",
                        info = scriptPubKey || 0x01 || le64(64))[0:32]

Constraints:
- `view_seed` derivation must be a one-to-one function of the same `ss` and
  `scriptPubKey`, so the two HKDF calls above yield byte-identical nonces.
- These are NOT two independent HKDFs: they share the same IKM source (`ss`)
  and the same `info`; only the salt differs and is bound to the role
  ("rcpu-pathc-v1" sender / "rcpu-rewind-v1" viewer).
- If the two derivations ever diverge, an exchange holding only `view_seed`
  cannot rewind: that is a spec bug, not a wallet bug.
- The final binding between `ss`, `view_seed`, and the rewind nonce must be
  fixed by test vectors before `BlindOutputToRecipientV2` is written.

## Wallet rules

- destination has spend pubkey (rcpux1) -> Path C
- change -> Path C
- bare rcpu1, no pubkey -> explicit plaintext, never Path A
- recipient_keys non-empty but a non-fee output lacks a key -> fail closed
- unblind dispatch by first byte; unknown prefix -> skip, not amount 0
- memo: this version keeps `message = nullptr` (matches current
  `rangeproof_sign(..., nullptr, 0, ...)`); a memo field requires a new
  version byte.

## Decisions (must be ratified before BlindOutputToRecipientV2)

| # | decision | recommendation | status |
|---|---|---|---|
| 1 | HKDF IKM is the raw 32-byte x (no `SHA256(x||y)` pre-hash) | YES — pre-hash is a new path, not Path C v1 | PENDING |
| 2 | info binding | FROZEN = raw scriptPubKey bytes of that output (P2TR: 34 bytes = `51 20 \|\| x-only`). Not x-only alone, not CTxOut. | LOCKED |
| 3 | Y parity: sender may use any Y, receiver tries even-Y then odd-Y (both attempts in vectors) | YES — never encode parity into the nonce field | PENDING |
| 4 | View key rewinds the SAME nonce as the sender: `nonce = HKDF(view_seed, salt="rcpu-rewind-v1", info = spk \|\| 0x01 \|\| le64(64))` must equal the sender's `HKDF(ss, salt="rcpu-pathc-v1", ...)`; view_seed derivation is one-to-one with (ss, spk); never two unrelated HKDF calls | FROZEN — same nonce by construction, spend key not required | LOCKED |
| 5 | One ephemeral per output; `-ctlegacy=1` reverts the whole tx to Path A, never mixes C/A in one non-fee set | YES — per-output key, whole-tx legacy switch | PENDING |

The five decisions above must be flipped to LOCKED before `BlindOutputToRecipientV2`
is written. Changing IKM, info binding, or the rewind derivation after release
would create a new path (future Path D).

## Implementation gate

- No fixed hex vectors, no merge into `src/blind.cpp`.
- All vectors are generated from this repository's own `secp256k1_ecdh`
  implementation; never hand-computed.

## Out of scope

Consensus Path A ban, Bulletproofs, Dandelion, Silent Payments.

## Appendix A — Path B regression vector

Purpose: any future Path C patch must keep this byte-identical.

Construction (matches src/blind.cpp ComputeECDHNonce + CopyX32):
  ss     = secp256k1_ecdh(recipient_pub, ephemeral_priv, hashfp=CopyX32)
  nonce  = ss                    # 32-byte x, NO SHA256, NO HKDF
  nNonce = compressed(ephemeral_pub)   # must start with 0x03

Values below were produced by compiling this repository's secp256k1 tree
(with ENABLE_MODULE_ECDH) and calling `secp256k1_ecdh` with the CopyX32
callback copied from blind.cpp; sender-side and receiver-side ECDH were
verified to agree (PASS), and the 0x02->0x03 re-roll loop of
BlindOutputToRecipient was exercised. Deterministic test scalars only;
no production keys. scriptPubKey is P2TR built from the recipient's x-only
public key (0x51 0x20 || x).

### Vector B1

| field | encoding | value |
|---|---|---|
| recipient_priv | 32B hex | 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 |
| recipient_pub  | 33B hex | 0284bf7562262bbd6940085748f3be6afa52ae317155181ece31b66351ccffa4b0 |
| ephemeral_priv | 32B hex | 2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f42 |
| ephemeral_pub  | 33B hex, prefix 0x03 | 0338be4e8cfa078d48299b557033a07024a46963c874c666f0cfb9b753425da2cc |
| ss = nonce     | 32B hex | 8ff9563819af439784eff54bd65e65614b14c7fe66b6b2f6010a72ec681f38f0 |
| amount         | CAmount | 500000000 |
| scriptPubKey   | raw hex | 512084bf7562262bbd6940085748f3be6afa52ae317155181ece31b66351ccffa4b0 |

Generator notes:
- ephemeral_priv starts at 0x2122...3f40; its pubkey had a 0x02 prefix, so the
  BlindOutputToRecipient re-roll loop incremented it twice to 0x2122...3f42,
  yielding prefix 0x03. This is exact behavior being frozen.
- recipient_pub = P2TR output key x-coordinate `84bf...a4b0`.

Required assertions after any blinding change:
1. ComputeECDHNonce(ephemeral_priv, recipient_pub) == ss
2. ComputeECDHNonce(recipient_priv, ephemeral_pub) == ss
3. UnblindValueWithKey(recipient_priv, commit, nNonce, proof) == amount
4. UnblindValue(...) on this output == false   # not Path A
5. GetOutputAmount(...) == nullopt for third party
6. nNonce[0] == 0x03

Negative:
- Flip nNonce[0] to 0x02 → must NOT treat X as Path A raw nonce
- Mutate any ss byte → rewind fails
- No HKDF on Path B: if `ss` changes after a Path C patch, CopyX32 was altered.

Regeneration: a small standalone C program calling the repository's
secp256k1_ecdh + CopyX32 reproduces these values deterministically; the
generator itself is not part of this PR (test vectors live in src/test).