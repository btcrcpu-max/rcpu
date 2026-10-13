## Summary

Restore Path A commitment shell for bare `rcpu1` addresses (no blinding pubkey), reverting the v1.1.5 plaintext fallback.

### What's changed

- **`rcpux1`** (keyed confidential address): still **Path C** (`0x04` nonce, HKDF rewind). No change.
- **Bare `rcpu1`** (no resolved pubkey): back to **Path A** (`0x02 || nonce` + commitment + rangeproof) instead of explicit plaintext `nValue`.

### What's NOT changed (intentionally frozen)

- Consensus: `nBanPathAHeight` stays `INT_MAX`; no chainparams edit.
- `CopyX32` / `ComputeECDHNonce` / Path C HKDF: untouched.
- Borromean rangeproof verify (max 5134 bytes): untouched.
- `receive.cpp` dispatch (`0x02/0x03/0x04`): untouched.
- Wallet-side mainnet ban check (spend.cpp 1546–1549): kept but inactive at `INT_MAX`.
- `BlindTransaction(..., use_path_c=true)` signature: untouched.

### Test coverage

- `blind_tx_mixed_path_c_and_path_a`: one keyed output → `0x04`, one key-less → `0x02` + commitment.
- `blind_tx_all_path_a_when_no_keys`: all-nullopt → all `0x02`, rewindable via `UnblindValue`.

### Impact

- Only **new** transactions to bare `rcpu1` get the `0x02` commitment shell.
- v1.1.7–v1.1.8 plaintext `nValue` outputs already on-chain **remain unchanged**.
- Path A amounts are still rewindable by anyone (nonce is public); this just blocks explorers that show commitments without unblinding.

---

**CI**: wait for `build` / `make-check` / `build-gui` / `functional-tests` all green before merge.
