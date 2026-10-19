// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_QHASH_H
#define BITCOIN_CRYPTO_QHASH_H

#include <cstddef>
#include <cstdint>

#include <uint256.h>

/**
 * RCPU qPoW ("qhash") — the second proof-of-work algorithm of the dual-algo
 * consensus (RandomX + qPoW), active from Consensus::Params::nDualAlgoActivationHeight.
 *
 * The function simulates a 16-qubit state vector (65536 amplitudes) entirely in
 * Q30 signed fixed-point integer arithmetic (one unit = 2^30). Every operation
 * is an int64 add/mul/shift with a fixed rounding rule, so the output is
 * bit-identical on all platforms and compilers (no floating point anywhere).
 *
 * Circuit per hash (deterministic, keyed by the 32-byte seed):
 *   2 rounds x [ 16 pseudo-random Ry(k*pi/8) single-qubit gates,
 *                16-gate CNOT ladder with rotating target offset,
 *                bit-reversal permutation of the state vector ],
 * then the 65536 amplitudes are folded into 64 int64 accumulators
 * (acc[j] = sum of amplitude[i] for i === j (mod 64)) and the 512-byte
 * accumulator block is hashed with sha256d.
 *
 * The full state participates in the final digest, so the work cannot be
 * shortcut by evaluating only part of the circuit.
 */

/** Compute the qhash of a 32-byte seed (consensus-critical, deterministic). */
uint256 QHashFromSeed(const unsigned char seed[32]);

#endif // BITCOIN_CRYPTO_QHASH_H
