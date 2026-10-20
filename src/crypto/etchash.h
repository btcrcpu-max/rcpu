// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_ETCHASH_H
#define BITCOIN_CRYPTO_ETCHASH_H

#include <cstdint>
#include <vector>

#include <uint256.h>

/**
 * Etchash (ECIP-1099, the Ethereum Classic variant of ethash) light
 * verification, ported from go-ethereum consensus/ethash/algorithm.go.
 *
 * RCPU-specific rules (consensus-critical):
 *   - epoch = (height - nDualAlgoActivationHeight) / nEtchashEpochLength
 *     (height-driven, computed by the caller; this module is epoch-pure)
 *   - seed_e = keccak256 applied e times to 32 zero bytes
 *   - verification is "light" only: hashimotoLight walks the shared cache via
 *     generateDatasetItem; no full DAG is ever materialized by the node
 *   - header_hash = Keccak256(SerializeRandomXHeader with hashRandomX zeroed),
 *     nonce8 = nNonce (4B LE) || 4 zero bytes (caller's job; this module
 *     takes the 32-byte header_hash and 64-bit nonce)
 *   - the block's hashRandomX field stores `result` (keccak256(seed||digest))
 */

namespace etchash {

constexpr int HASH_BYTES = 64;       //!< hash length in bytes
constexpr int HASH_WORDS = 16;       //!< number of 32-bit words in a hash
constexpr int MIX_BYTES = 128;       //!< width of mix
constexpr int MIX_WORDS = 32;        //!< number of 32-bit words in the mix
constexpr int DATASET_PARENTS = 256; //!< number of parents of each dataset element
constexpr int CACHE_ROUNDS = 3;      //!< number of rounds in cache production
constexpr int LOOP_ACCESSES = 64;    //!< number of accesses in hashimoto loop
constexpr uint64_t CACHE_INIT_BYTES = 1 << 24;   //!< cache size at epoch 0
constexpr uint64_t CACHE_GROWTH_BYTES = 1 << 17; //!< cache growth per epoch

/** Exact primality test for n < 2^64 (deterministic Miller-Rabin, 7 bases). */
bool IsPrime(uint64_t n);

/** Cache size for an epoch: highest prime-bounded size below the linear
 *  threshold, per geth calcCacheSize. */
uint64_t CalcCacheSize(uint64_t epoch);

/** seed_e = keccak256^epoch(32 zero bytes). */
uint256 SeedForEpoch(uint64_t epoch);

/** Generate the verification cache for a seed (geth generateCache).
 *  Returns cache as little-endian uint32 words, size/4 entries. */
std::vector<uint32_t> GenerateCache(uint64_t cache_size, const uint256& seed);

/** Generate one 64-byte dataset item from the cache (geth generateDatasetItem).
 *  `cache_words` is the cache as uint32 LE words; `index` is the item index. */
void GenerateDatasetItem(const std::vector<uint32_t>& cache_words, uint32_t index,
                         unsigned char out[HASH_BYTES]);

/** hashimotoLight: run the 64-access mix loop using on-the-fly dataset item
 *  generation from the shared cache. Outputs the 32-byte compressed digest
 *  and the 32-byte result = keccak256(seed64 || digest). */
void HashimotoLight(uint64_t cache_size, const std::vector<uint32_t>& cache_words,
                    const unsigned char header_hash[32], uint64_t nonce,
                    unsigned char digest_out[32], unsigned char result_out[32]);

} // namespace etchash

#endif // BITCOIN_CRYPTO_ETCHASH_H
