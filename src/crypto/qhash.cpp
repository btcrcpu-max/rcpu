// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/qhash.h>

#include <crypto/sha256.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Fixed-point format: one real unit is represented by 2^30 (Q30, signed).
// ---------------------------------------------------------------------------
constexpr int64_t Q30_ONE = int64_t(1) << 30;
constexpr int64_t Q30_HALF = int64_t(1) << 29;

constexpr unsigned NQUBITS = 16;
constexpr unsigned NAMP = unsigned(1) << NQUBITS; // 65536 amplitudes
constexpr unsigned NROUNDS = 2;

// cos(k*pi/8) and sin(k*pi/8) in Q30, k = 1..8, rounded half-away-from-zero.
// Consensus constants: the circuit selects among these 8 exact matrices; no
// libm trigonometry is ever used at hashing time.
constexpr int64_t Q_COS[8] = {
    992008094,   //  cos(pi/8)
    759250125,   //  cos(pi/4)
    410903207,   //  cos(3*pi/8)
    0,           //  cos(pi/2)
    -410903207,  //  cos(5*pi/8)
    -759250125,  //  cos(3*pi/4)
    -992008094,  //  cos(7*pi/8)
    -1073741824, //  cos(pi)
};
constexpr int64_t Q_SIN[8] = {
    410903207,  //  sin(pi/8)
    759250125,  //  sin(pi/4)
    992008094,  //  sin(3*pi/8)
    1073741824, //  sin(pi/2)
    992008094,  //  sin(5*pi/8)
    759250125,  //  sin(3*pi/4)
    410903207,  //  sin(7*pi/8)
    0,          //  sin(pi)
};

// Fixed rounding rule: (v + 2^29) >> 30 rounds half toward +infinity,
// identically on every platform (C++20 arithmetic shift for signed types).
inline int64_t Q30_Mul(int64_t a, int64_t b)
{
    return (a * b + Q30_HALF) >> 30;
}

inline unsigned Rev16(unsigned i)
{
    // Bit-reversal of a 16-bit index.
    i = ((i & 0x5555u) << 1) | ((i >> 1) & 0x5555u);
    i = ((i & 0x3333u) << 2) | ((i >> 2) & 0x3333u);
    i = ((i & 0x0F0Fu) << 4) | ((i >> 4) & 0x0F0Fu);
    i = ((i & 0x00FFu) << 8) | ((i >> 8) & 0x00FFu);
    return i;
}

/** Expand the 32-byte seed into a keystream (sha256 counter mode). */
inline void KeystreamBlocks(const unsigned char seed[32], unsigned char* out, size_t n)
{
    size_t done = 0;
    uint32_t ctr = 0;
    unsigned char ctr_le[4];
    unsigned char buf[32];
    while (done < n) {
        // Counter serialized little-endian: the keystream must not depend on
        // the host byte order (consensus-critical determinism).
        ctr_le[0] = static_cast<unsigned char>(ctr & 0xFFu);
        ctr_le[1] = static_cast<unsigned char>((ctr >> 8) & 0xFFu);
        ctr_le[2] = static_cast<unsigned char>((ctr >> 16) & 0xFFu);
        ctr_le[3] = static_cast<unsigned char>((ctr >> 24) & 0xFFu);
        CSHA256().Write(seed, 32).Write(ctr_le, 4).Finalize(buf);
        const size_t take = std::min(sizeof(buf), n - done);
        memcpy(out + done, buf, take);
        done += take;
        ++ctr;
    }
}

} // namespace

uint256 QHashFromSeed(const unsigned char seed[32])
{
    // State vector: 65536 Q30 amplitudes. thread_local gives each validation /
    // mining thread its own 512 KiB workspace. |amplitude| never exceeds
    // ~1.04 units because the rotations are (approximately) orthogonal and the
    // accumulated rounding drift over 32 gates is bounded far below 1 LSB of
    // overflow headroom.
    thread_local std::vector<int64_t> amp(NAMP, 0);

    // |000...0> with amplitude 1 at index 0.
    for (unsigned i = 0; i < NAMP; ++i) amp[i] = 0;
    amp[0] = Q30_ONE;

    // Keystream: NROUNDS x (16 single-qubit gate selector bytes + 1 CNOT offset
    // byte) = 34 bytes for the current parameterization.
    constexpr size_t NKEY = size_t(NROUNDS) * (NQUBITS + 1);
    unsigned char key[NKEY];
    KeystreamBlocks(seed, key, NKEY);

    for (unsigned r = 0; r < NROUNDS; ++r) {
        const unsigned char* kr = key + size_t(r) * (NQUBITS + 1);

        // --- Single-qubit rotation layer -------------------------------
        for (unsigned q = 0; q < NQUBITS; ++q) {
            const unsigned char b = kr[q];
            const unsigned kidx = b & 7u;        // k = 1..8 -> theta = k*pi/8
            const bool neg = (b >> 7) != 0;      // negative angle
            const int64_t c = Q_COS[kidx];
            const int64_t s = Q_SIN[kidx];
            const unsigned bit = 1u << q;
            for (unsigned i = 0; i < NAMP; ++i) {
                if (i & bit) continue;
                const int64_t x = amp[i];
                const int64_t y = amp[i | bit];
                int64_t nx, ny;
                if (!neg) {
                    nx = Q30_Mul(c, x) - Q30_Mul(s, y);
                    ny = Q30_Mul(s, x) + Q30_Mul(c, y);
                } else {
                    nx = Q30_Mul(c, x) + Q30_Mul(s, y);
                    ny = Q30_Mul(-s, x) + Q30_Mul(c, y);
                }
                amp[i] = nx;
                amp[i | bit] = ny;
            }
        }

        // --- CNOT ladder ------------------------------------------------
        const unsigned offset = kr[NQUBITS] & 15u;
        for (unsigned q = 0; q < NQUBITS; ++q) {
            const unsigned t = (q + 1u + offset) & 15u;
            if (t == q) continue;
            const unsigned cbit = 1u << q;
            const unsigned tbit = 1u << t;
            for (unsigned i = 0; i < NAMP; ++i) {
                // Swap pairs (i, i ^ tbit) where control bit set, target clear;
                // each unordered pair is touched exactly once.
                if ((i & cbit) && !(i & tbit)) {
                    std::swap(amp[i], amp[i ^ tbit]);
                }
            }
        }

        // --- Bit-reversal permutation -----------------------------------
        for (unsigned i = 0; i < NAMP; ++i) {
            const unsigned j = Rev16(i);
            if (i < j) std::swap(amp[i], amp[j]);
        }
    }

    // --- Fold: every amplitude participates in the final digest ---------
    int64_t acc[64];
    memset(acc, 0, sizeof(acc));
    for (unsigned i = 0; i < NAMP; ++i) {
        acc[i & 63u] += amp[i];
    }

    unsigned char fold[512];
    for (unsigned j = 0; j < 64; ++j) {
        uint64_t v = static_cast<uint64_t>(acc[j]);
        for (unsigned b = 0; b < 8; ++b) {
            fold[size_t(j) * 8 + b] = static_cast<unsigned char>(v & 0xFFu);
            v >>= 8;
        }
    }

    unsigned char h1[32], out[32];
    CSHA256().Write(fold, sizeof(fold)).Finalize(h1);
    CSHA256().Write(h1, 32).Finalize(out);

    uint256 result;
    memcpy(result.begin(), out, 32);
    return result;
}
