# RandomX Mining on RCPU

This document describes the RandomX proof-of-work implementation, derived from
`src/pow.cpp` and `src/kernel/chainparams.cpp`.

## Overview

RCPU replaces SHA-256 with **RandomX**, a CPU-optimized PoW algorithm designed
to be ASIC-resistant and GPU-resistant.

| Parameter | Mainnet value | Source reference |
|-----------|---------------|------------------|
| PoW algorithm | RandomX | `src/kernel/chainparams.cpp` (`fPowRandomX`) |
| Epoch duration | **7 days** (604,800 s) | `src/kernel/chainparams.cpp` (`nRandomXEpochDuration`) |
| Fast mode default | Enabled | `src/pow.cpp` (`DEFAULT_RANDOMX_FAST_MODE`) |
| Config option | `-randomxfastmode` | Toggle fast mode (full dataset) |
| VM cache size | `-randomxvmcachesize` | LRU cache for VMs |

## Epoch and Seed Derivation

The RandomX epoch is computed from the block timestamp:

```
epoch = block_timestamp / epoch_duration
```

The seed hash (the RandomX key) is derived by double-SHA256 of the string:

```
"RCPU/RandomX/Epoch/<epoch_number>"
```

This means:

- The seed is **deterministic from the block timestamp** -- no on-chain
  seed vote or committee is needed.
- Anyone can compute the seed for any epoch without syncing the chain.
- The seed changes every 7 days (mainnet) or 1 day (regtest).

Source: `src/pow.cpp`, `GetEpoch()` and `GetSeedHash()`.

## Light Mode vs Fast Mode

- **Light mode** (default during IBD): Uses a small cache (~256 MB).
  All nodes and pools can run in light mode.
- **Fast mode** (`-randomxfastmode=1`): Allocates the full dataset (~1 GB+)
  for faster hashing. Created in a background thread after IBD completes.
- Pools and miners should enable fast mode for performance.
- Light nodes can still validate blocks; they just hash slower.

## Epoch Switching and Validation

- When a block's timestamp crosses into a new epoch, a new RandomX VM must
  be initialized for that epoch.
- The first block of a new epoch may take **longer to validate** because
  the VM cache must be initialized (~seconds in light mode).
- Miners should pre-compute the next epoch's VM before the transition to
  avoid stale shares.
- Pools must track epoch boundaries and switch VMs accordingly.

## Pool Operator Notes

- The pool's `getblocktemplate` RPC returns `rx_epoch` and
  `rx_epoch_duration` fields.
- Pools should monitor the current block timestamp against the epoch
  boundary and pre-initialize the next epoch's VM.
- Stratum variance should account for epoch switch latency.
- `getblock` RPC returns `rx_cm` (RandomX commitment) and `rx_hash`
  (RandomX hash) fields for block verification.

## getblocktemplate RandomX Fields

`getblocktemplate` exposes the following RandomX-specific fields (RCPU):

| Field | Type | Meaning |
|-------|------|---------|
| `rx_epoch_duration` | num | Epoch duration in seconds |
| `rx_epoch` | num | Epoch of the template timestamp (`time / rx_epoch_duration`); recompute from the final block time if it is adjusted |
| `rx_seed_hash` | hex | RandomX key for `rx_epoch` |
| `rx_header_size` | num | Serialized header size fed to RandomX: **112** (80-byte legacy header + 32-byte `hashRandomX`) |
| `rx_hash_field` | str | `hashRandomX` location/semantics: 32 bytes at offset 80; **zeroed** during hashing and commitment, filled with the resulting RandomX hash before submission |
| `rx_commitment_target` | hex | Maximum acceptable RandomX commitment (same 256-bit value as `target`) |
| `rx_pow_rule` | str | PoW rule identifier, currently `"randomx-v1"` |

## Minimal Reference Miner

A minimal pool/miner must perform the following steps. All RandomX
computations use the same underlying library calls as `src/pow.cpp`.

### 1. Fetch a template

```
getblocktemplate '{"rules": ["segwit"]}'
```

Record, at minimum: `previousblockhash`, `transactions`, `coinbasevalue`,
`target`, `bits`, `curtime`, `noncerange`, and the `rx_*` fields above.

### 2. Assemble the 112-byte header

Serialize the 80-byte header as usual (`version`, `prevhash`, `merkleroot`,
`time`, `bits`, `nonce`), then append a **32-byte zeroed** `hashRandomX`
field:

```
header112 = legacy80(version, prev, merkle, time, bits, nonce) || 0x00 * 32
```

Set `time` no earlier than `mintime` and keep `bits` unchanged.

### 3. Compute the RandomX hash

Determine the epoch from the **final** block time:

```
epoch = time / rx_epoch_duration
seed  = sha256d("RCPU/RandomX/Epoch/%d" % epoch)   # == rx_seed_hash of that epoch
```

Initialize a RandomX VM with `seed`, hash `header112` and obtain the
32-byte RandomX hash:

```
rx_hash = randomx_calculate_hash(vm, header112, 112)
```

### 4. Fill `hashRandomX` and build the commitment

Copy `rx_hash` into the `hashRandomX` field of the header. The commitment is
computed over the header **with `hashRandomX` zeroed again**:

```
cm_header      = header112_with_hashRandomX_zeroed
commitment     = randomx_calculate_commitment(cm_header, 112, rx_hash)
```

### 5. Solve

Iterate the 4-byte `nonce` (and optionally `time` within `mintime`..
`curtime + 7200`) until the **commitment** is `<= rx_commitment_target`
(equivalently `<= target`). The RandomX hash does not need to be below the
target on its own — the consensus check only requires the commitment to meet
the target. Fast-mode (`-randomxfastmode=1`) is strongly recommended for
miners.

### 6. Submit

Submit the full serialized block (112-byte header + transactions) to
`submitblock`. The header in the submission must carry the computed
`rx_hash` in `hashRandomX`; the node independently re-derives the commitment
and, for full verification, re-hashes the header to confirm the value.

## Dual-Algo (RandomX + Etchash) Mining

From block height **21,000** (mainnet/testnet; 150 on regtest) RCPU runs
dual-algo free competition (see `doc/consensus-params.md`):

| Parameter | Mainnet value | Source reference |
|-----------|---------------|------------------|
| Activation height | **21,000** (mainnet/testnet); **150** (regtest) | `src/kernel/chainparams.cpp` (`nDualAlgoActivationHeight`) |
| Version bit | **bit 30** (`0x40000000`, `BLOCK_VERSION_QHASH`) | `src/primitives/block.h` |
| Second algorithm | **Etchash** (ECIP-1099 light verification, `fDualAlgoEtchash=true`) | `src/kernel/chainparams.cpp`; `src/pow.cpp` (`CheckProofOfWorkEtchash`) |
| Etchash epoch length | **6000** blocks | `src/kernel/chainparams.cpp` (`nEtchashEpochLength`) |
| Target spacing | 600 s (10 min) per algorithm | `src/kernel/chainparams.cpp` (`nDualAlgoTargetSpacing`) |

- Blocks with bit 30 set are mined/verified with **Etchash** (light
  verification: the node builds only the epoch cache, never the full DAG).
- All other blocks use RandomX as described above.
- Each algorithm runs its own ASERT difficulty instance anchored at
  height 20,999 with a reference target of `nBits >> 1`.

### Etchash epoch and seed — pools must use `getblocktemplate`

The Etchash epoch is **height-driven**: `epoch = (height - 21000) / 6000` on
mainnet/testnet. `getblocktemplate` for a bit-30 template returns
`algo = "etchash"` plus:

| Field | Meaning |
|---|---|
| `etchash_epoch` | Etchash epoch for the template height |
| `etchash_epoch_length` | Epoch length in blocks (6000) |
| `etchash_seed_hash` | 32-byte epoch seed (keccak256^epoch of 32 zero bytes) |
| `etchash_cache_size` | Light-verification cache size for the epoch (informational) |
| `etchash_dataset_size` | Full DAG size for the epoch (standard ethash `calcDatasetSize`) |
| `etchash_dataset_rows` | `dataset_size / 128` — the row count hashimotoLight indexes |
| `etchash_header_size` | Serialized header size fed to the hash (**112**) |
| `etchash_hash_field` | `hashRandomX` semantics for this template |
| `etchash_commitment_target` | Maximum acceptable Etchash result (same 256-bit value as `target`) |
| `etchash_pow_rule` | PoW rule identifier (`"etchash-ecip1099-light-v1"`) |

**Pools MUST take `etchash_epoch` / `etchash_seed_hash` from
`getblocktemplate` and MUST NOT recompute the epoch/seed from an assumed
activation height.** The cache/seed derivation (Algorithm 1 in ECIP-1099) is
standard Etchash: `seed = keccak256^epoch(32 zero bytes)`, augmented with the
standard 64-byte header mix in the final digest
(`result = keccak256(seed64 || digest)`, as enforced by
`CheckProofOfWorkEtchash`). The full-DAG size is the standard ethash
`calcDatasetSize(epoch)` (1 GiB + 8 MiB/epoch, prime-bounded), and
`hashimotoLight` uses `dataset_size / 128` rows. Only the epoch length is
RCPU-specific (6000 instead of 30000), because the epoch is height-driven.
If the pool needs the full DAG to be generated locally it builds it from
`etchash_seed_hash`; the node itself stays light.
