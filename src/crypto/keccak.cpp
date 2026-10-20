// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/keccak.h>

#include <cstring>

/**
 * Keccak-f[1600] permutation, straightforward portable implementation.
 * Rotation offsets and lane-index permutation follow the reference
 * (keccak-tiny / "Keccak implementation overview" v3.4) tables:
 *   rho: lane (x,y) rotated by r[x + 5*y]
 *   pi:  lane (x,y) moves to (y, 2*x + 3*y)
 */

namespace {

inline uint64_t rotl64(uint64_t x, int n)
{
    return (x << n) | (x >> (64 - n));
}

constexpr int KECCAK_RHO[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
    27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
};

constexpr int KECCAK_PI[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
    15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
};

constexpr uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

void keccakf(uint64_t st[25])
{
    for (int round = 0; round < 24; ++round) {
        // Theta
        uint64_t bc[5];
        for (int i = 0; i < 5; ++i) {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for (int i = 0; i < 5; ++i) {
            uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) {
                st[j + i] ^= t;
            }
        }

        // Rho + Pi
        uint64_t t = st[1];
        for (int i = 0; i < 24; ++i) {
            int j = KECCAK_PI[i];
            uint64_t tmp = st[j];
            st[j] = rotl64(t, KECCAK_RHO[i]);
            t = tmp;
        }

        // Chi
        for (int j = 0; j < 25; j += 5) {
            uint64_t row[5];
            for (int i = 0; i < 5; ++i) row[i] = st[j + i];
            for (int i = 0; i < 5; ++i) {
                st[j + i] ^= (~row[(i + 1) % 5]) & row[(i + 2) % 5];
            }
        }

        // Iota
        st[0] ^= KECCAK_RC[round];
    }
}

inline uint64_t load64(const unsigned char* x)
{
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= uint64_t(x[i]) << (8 * i);
    return r;
}

inline void store64(unsigned char* x, uint64_t u)
{
    for (int i = 0; i < 8; ++i) x[i] = (unsigned char)(u >> (8 * i));
}

/** Absorb exactly RATE bytes (one full block) and permute. */
void keccak_absorb_block(uint64_t st[25], const unsigned char* block, size_t rate)
{
    for (size_t i = 0; i < rate / 8; ++i) {
        st[i] ^= load64(block + 8 * i);
    }
    keccakf(st);
}

} // namespace

CKeccak256::CKeccak256()
{
    std::memset(m_state, 0, sizeof(m_state));
    m_buflen = 0;
}

CKeccak256& CKeccak256::Write(const unsigned char* data, size_t len)
{
    while (len > 0) {
        size_t take = RATE - m_buflen;
        if (take > len) take = len;
        std::memcpy(m_buf + m_buflen, data, take);
        m_buflen += take;
        data += take;
        len -= take;
        if (m_buflen == RATE) {
            keccak_absorb_block(m_state, m_buf, RATE);
            m_buflen = 0;
        }
    }
    return *this;
}

void CKeccak256::Finalize(unsigned char hash[OUTPUT_SIZE])
{
    std::memset(m_buf + m_buflen, 0, RATE - m_buflen);
    m_buf[m_buflen] ^= 0x01;
    m_buf[RATE - 1] ^= 0x80;
    keccak_absorb_block(m_state, m_buf, RATE);
    for (size_t i = 0; i < OUTPUT_SIZE / 8; ++i) store64(hash + 8 * i, m_state[i]);
    // tail bytes (none for 256: 32 % 8 == 0)
    Reset();
}

CKeccak256& CKeccak256::Reset()
{
    std::memset(m_state, 0, sizeof(m_state));
    std::memset(m_buf, 0, sizeof(m_buf));
    m_buflen = 0;
    return *this;
}

CKeccak512::CKeccak512()
{
    std::memset(m_state, 0, sizeof(m_state));
    m_buflen = 0;
}

CKeccak512& CKeccak512::Write(const unsigned char* data, size_t len)
{
    while (len > 0) {
        size_t take = RATE - m_buflen;
        if (take > len) take = len;
        std::memcpy(m_buf + m_buflen, data, take);
        m_buflen += take;
        data += take;
        len -= take;
        if (m_buflen == RATE) {
            keccak_absorb_block(m_state, m_buf, RATE);
            m_buflen = 0;
        }
    }
    return *this;
}

void CKeccak512::Finalize(unsigned char hash[OUTPUT_SIZE])
{
    std::memset(m_buf + m_buflen, 0, RATE - m_buflen);
    m_buf[m_buflen] ^= 0x01;
    m_buf[RATE - 1] ^= 0x80;
    keccak_absorb_block(m_state, m_buf, RATE);
    for (size_t i = 0; i < OUTPUT_SIZE / 8; ++i) store64(hash + 8 * i, m_state[i]);
    Reset();
}

CKeccak512& CKeccak512::Reset()
{
    std::memset(m_state, 0, sizeof(m_state));
    std::memset(m_buf, 0, sizeof(m_buf));
    m_buflen = 0;
    return *this;
}

void Keccak256(const unsigned char* data, size_t len, unsigned char out[32])
{
    CKeccak256 h;
    h.Write(data, len);
    h.Finalize(out);
}

uint256 Keccak256(const uint256& in)
{
    uint256 out;
    Keccak256(in.begin(), in.size(), out.begin());
    return out;
}

void Keccak512(const unsigned char* data, size_t len, unsigned char out[64])
{
    CKeccak512 h;
    h.Write(data, len);
    h.Finalize(out);
}
