// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_KECCAK_H
#define BITCOIN_CRYPTO_KECCAK_H

#include <cstddef>
#include <cstdint>

#include <uint256.h>

/**
 * Original Keccak (pre-NIST-SHA3 variant, padding 0x01 ... 0x80), as used by
 * Ethereum / ethash / etchash. This is NOT FIPS SHA-3 (which uses padding
 * 0x06): the two produce different digests for the same input.
 *
 * Consensus-critical: etchash header hashing, seed derivation, cache
 * generation and the hashimoto compression all use these primitives.
 */

/** Streaming Keccak-256 hasher (rate 136 bytes). */
class CKeccak256
{
public:
    static constexpr size_t OUTPUT_SIZE = 32;
    static constexpr size_t RATE = 136;

    CKeccak256();
    CKeccak256& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CKeccak256& Reset();

private:
    uint64_t m_state[25];      //!< Keccak-f[1600] state, lanes in host order
    unsigned char m_buf[RATE]; //!< partially filled rate block
    size_t m_buflen;           //!< bytes currently in m_buf
};

/** Streaming Keccak-512 hasher (rate 72 bytes). */
class CKeccak512
{
public:
    static constexpr size_t OUTPUT_SIZE = 64;
    static constexpr size_t RATE = 72;

    CKeccak512();
    CKeccak512& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CKeccak512& Reset();

private:
    uint64_t m_state[25];
    unsigned char m_buf[RATE];
    size_t m_buflen;
};

/** One-shot Keccak-256. */
void Keccak256(const unsigned char* data, size_t len, unsigned char out[32]);

/** One-shot Keccak-256 over a 32-byte input, returning uint256. */
uint256 Keccak256(const uint256& in);

/** One-shot Keccak-512. */
void Keccak512(const unsigned char* data, size_t len, unsigned char out[64]);

#endif // BITCOIN_CRYPTO_KECCAK_H
