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
