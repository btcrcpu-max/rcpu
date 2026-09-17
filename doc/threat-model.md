# RCPU threat model

This document states what Confidential Transactions (CT) and related
subsystems do and do not hide. If this file disagrees with consensus
code, the code wins.

Last aligned with: post-v1.0.16 tree (A2/B2, CVE-2024-52911 ConnectBlock
guard, path-A nonce prefix gate, optional GetOutputAmount) plus the
`ct/default-ecdh` wallet default.

## 1. Actors

| Actor | Can see |
|---|---|
| Global observer (P2P, explorer, mempool) | Graph of txids, scripts, fees, coinbase subsidy, rangeproofs, commitments |
| Sender of a path-A output | Amount of that output (nonce is on-chain as `0x02 || 32 bytes`) |
| Sender of a path-B output | Amount (they created the blinding factor) |
| Recipient of a path-B output | Amount via ECDH + `UnblindValueWithKey` |
| Third party without keys | Path-B amount: no. Path-A amount: yes |
| Miner / fee estimator | Explicit fee output (by design) |

CT hides **output amounts** of path-B (and well-formed path-A only from
parties who lack the nonce). It does **not** hide addresses beyond
what Bitcoin already does, does not hide values of explicit outputs,
and does not hide the fee.

## 2. Two blinding paths

Nonce commitment is always 33 bytes. The first byte selects the path.

### Path A — `BlindOutput` (`0x02 || raw_nonce`)

- Pedersen commit + rangeproof as usual.
- The 32-byte rewind nonce is written on-chain after a fixed `0x02`
  prefix (`SetNonce` / `IsLegacyNonceCommit`).
- Anyone who parses the prefix can `GetNonce` and `UnblindValue`.
- **Confidentiality: format only.** Amount is recoverable by every
  full node and explorer that implements path A.
- Allowed after `ct/default-ecdh`:
  - unit tests
  - `-ctlegacy=1` (wallet-wide fallback)
  - **not** external wallet sends when a recipient pubkey exists

`GetNonce` returns the zero nonce if length ≠ 33 or prefix ≠ `0x02`.
Callers must not treat that zero as a live nonce. `GetOutputAmount`
returns `std::nullopt` on unblind failure so a failed rewind is not
confused with a real zero amount.

### Path B — ECDH (`BlindOutputToRecipient` / `BlindTransaction` with key)

- Sender samples an ephemeral key, ECDH with the recipient spend
  pubkey, derives the rewind nonce from the shared secret (`CopyX32`,
  no HKDF; see §5).
- The 33-byte field carries a compressed ephemeral **public** key.
  Implementation uses a `0x03` prefix on that pubkey so a path-A
  decoder (`0x02` only) cannot treat the field as a raw nonce.
- Recipient unblinds with `UnblindValueWithKey` and their private key.
- Sender still knows the amount (they chose `blind` and `amount`).
- Third parties without the recipient key cannot rewind.

Wallet rule (`CreateTransactionInternal` → `BlindTransaction`):

- `recipient_keys` empty: entire tx stays path A (legacy / tests /
  `-ctlegacy=1`).
- `recipient_keys` non-empty: length equals `vout`; every non-fee
  output must have a pubkey; missing key → **fail closed** (do not
  silently write path A).
- Change: wallet supplies its own pubkey and uses path B.

## 3. What stays explicit (not a bug)

### Fees

Each transaction has an explicit fee-shaped output so:

1. **Consensus can check money conservation without rewind.**
   Inputs and confidential outputs are Pedersen commitments. The fee
   must be an explicit `CAmount` so `in_commitments - out_commitments`
   equals `commit(fee)` (plus blinds that sum to zero). If the fee
   were confidential, every node would need a rewind nonce or a
   separate proof that the leftover equals a positive fee; RCPU does
   not implement that extra proof.
2. **Relay and mining policy** (`minrelaytxfee`, block template
   ranking, fee estimation) need a public sat/vB. A hidden fee would
   make DoS and fee-market rules unverifiable for peers.
3. **Fee outputs are not UTXOs.** Consensus excludes the fee-shaped
   output from the UTXO set and rejects a fee-shaped coinbase. The
   amount is public; it is not spendable as a coin.

So: observers always see *how much* was paid to miners. They do not
need that number to equal any path-B output amount.

### Coinbase / subsidy

The block reward and explicit coinbase value are public. Confidential
coinbase is rejected. Subsidy schedule is not a privacy feature
(5000 RCPU, 210000-block halvings, 1 RCPU tail, no hard cap).

### Graph, scripts, rangeproofs

- Which outpoints are spent, which scripts appear, tx size, and
  rangeproof blobs are public (A2 caps rangeproof at 5134 bytes).
- B2: a confidential UTXO cannot be spent by a legacy v2 explicit
  spend. That is consensus integrity, not extra hiding.

## 4. Wallet and RPC pitfalls

- `GetOutputAmount` → `std::optional<CAmount>`. UI/RPC must use
  `.value_or` only when a fallback is explicit; do not treat
  `nullopt` as “zero coins”.
- Path-A explorers will still display amounts for old outputs. New
  external sends should be path B so later explorers cannot rewind.
- Bech32 `rcpu1` only on mainnet. Base58 templates exist in code for
  compatibility and must not be used for payment.
- Default RPC auth is the datadir `.cookie`. Do not put
  `-rpcpassword` on the command line in docs or scripts.

## 5. Crypto constraints (will not “just fix”)

| Item | Status | Why it stays |
|---|---|---|
| `CopyX32` (ECDH shared secret → nonce, no SHA256/HKDF) | Compatibility with existing path-B outputs | Changing the KDF splits the chain of who can unblind historical path-B UTXOs. A new version would need a new prefix or version bit. |
| Path-A on-chain nonce | Legacy + `-ctlegacy` | Removing it is a wallet policy change, not consensus. |
| Single-asset blinds | `BlindTransaction` balances one asset | Multi-asset CT is out of scope. |
| RandomX | PoW, not CT | Custom patch vs upstream v1.2.1 must stay documented in the RandomX build scripts; swapping vanilla RandomX forks the chain. |

## 6. Node / p2p (non-CT)

- CVE-2024-52911: `ConnectBlock` must declare `txsdata` before
  `CCheckQueueControl` and leave through one exit after `Wait()`.
  Lint: `test/lint/lint-cve-52911-connectblock.sh`.
- 52921 / 52922 are already in the 27.0 baseline.
- Other Core 28/29/30 advisories are tracked separately; this file
  does not claim they are all backported.

## 7. Intentional non-fixes

- Tail emission (1 RCPU/block after 10 halvings) and no hard cap.
- Public fee and public coinbase.
- No decoy inputs / no ring signatures (this is not Monero).
- H-3 class items previously marked “will not fix” stay out of this
  PR unless a new advisory says otherwise.