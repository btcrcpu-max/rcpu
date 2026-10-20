// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/etchash.h>

#include <crypto/keccak.h>

#include <cstring>

namespace etchash {

namespace {

inline uint32_t fnv1a(uint32_t a, uint32_t b)
{
    return a * 0x01000193U ^ b;
}

inline void fnv_hash(uint32_t* mix, const uint32_t* data, int n)
{
    for (int i = 0; i < n; ++i) {
        mix[i] = mix[i] * 0x01000193U ^ data[i];
    }
}

inline uint32_t load32(const unsigned char* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void store32(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m)
{
    // (a * b) % m without overflow, Russian-peasant style. Used only by the
    // Miller-Rabin test; epoch-derived values are small so this is cheap.
    uint64_t r = 0;
    while (b) {
        if (b & 1) {
            r += a;
            if (r >= m || r < a) r -= m;
        }
        a <<= 1;
        if (a >= m) a -= m;
        b >>= 1;
    }
    return r;
}

uint64_t powmod(uint64_t base, uint64_t exp, uint64_t m)
{
    uint64_t r = 1 % m;
    base %= m;
    while (exp) {
        if (exp & 1) r = mulmod(r, base, m);
        base = mulmod(base, base, m);
        exp >>= 1;
    }
    return r;
}

} // namespace

bool IsPrime(uint64_t n)
{
    if (n < 2) return false;
    static constexpr uint64_t SMALL_PRIMES[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    for (uint64_t p : SMALL_PRIMES) {
        if (n % p == 0) return n == p;
    }
    uint64_t d = n - 1;
    int r = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        ++r;
    }
    // Deterministic Miller-Rabin bases, exact for n < 2^64.
    static constexpr uint64_t BASES[] = {2, 325, 9375, 28178, 450775, 9780504, 1795265022};
    for (uint64_t a : BASES) {
        a %= n;
        if (a == 0) continue;
        uint64_t x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composite = true;
        for (int i = 0; i < r - 1; ++i) {
            x = mulmod(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

uint64_t CalcCacheSize(uint64_t epoch)
{
    uint64_t size = CACHE_INIT_BYTES + CACHE_GROWTH_BYTES * epoch - HASH_BYTES;
    while (!IsPrime(size / HASH_BYTES)) {
        size -= 2 * HASH_BYTES;
    }
    return size;
}

uint256 SeedForEpoch(uint64_t epoch)
{
    uint256 seed; // 32 zero bytes
    for (uint64_t i = 0; i < epoch; ++i) {
        seed = Keccak256(seed);
    }
    return seed;
}

std::vector<uint32_t> GenerateCache(uint64_t cache_size, const uint256& seed)
{
    std::vector<unsigned char> cache(cache_size);
    const uint64_t rows = cache_size / HASH_BYTES;

    // Sequentially produce the initial dataset.
    Keccak512(seed.begin(), seed.size(), cache.data());
    for (uint64_t offset = HASH_BYTES; offset < cache_size; offset += HASH_BYTES) {
        Keccak512(cache.data() + offset - HASH_BYTES, HASH_BYTES, cache.data() + offset);
    }

    // Low-round RandMemoHash.
    unsigned char temp[HASH_BYTES];
    for (int round = 0; round < CACHE_ROUNDS; ++round) {
        for (uint64_t j = 0; j < rows; ++j) {
            const uint64_t src_off = ((j - 1 + rows) % rows) * HASH_BYTES;
            const uint64_t dst_off = j * HASH_BYTES;
            const uint64_t xor_off = (load32(cache.data() + dst_off) % uint32_t(rows)) * HASH_BYTES;
            for (int k = 0; k < HASH_BYTES; ++k) {
                temp[k] = cache[src_off + k] ^ cache[xor_off + k];
            }
            Keccak512(temp, HASH_BYTES, cache.data() + dst_off);
        }
    }

    // Reinterpret as little-endian uint32 words.
    std::vector<uint32_t> words(cache_size / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = load32(cache.data() + 4 * i);
    }
    return words;
}

void GenerateDatasetItem(const std::vector<uint32_t>& cache_words, uint32_t index,
                         unsigned char out[HASH_BYTES])
{
    const uint32_t rows = uint32_t(cache_words.size() / HASH_WORDS);

    // Initialize the mix: word 0 ^= index, rest copied.
    unsigned char mix[HASH_BYTES];
    store32(mix, cache_words[(index % rows) * HASH_WORDS] ^ index);
    for (int i = 1; i < HASH_WORDS; ++i) {
        store32(mix + 4 * i, cache_words[(index % rows) * HASH_WORDS + i]);
    }
    unsigned char mix_hash[HASH_BYTES];
    Keccak512(mix, HASH_BYTES, mix_hash);

    uint32_t int_mix[HASH_WORDS];
    for (int i = 0; i < HASH_WORDS; ++i) {
        int_mix[i] = load32(mix_hash + 4 * i);
    }

    // fnv it with a lot of random cache nodes based on index.
    for (uint32_t i = 0; i < DATASET_PARENTS; ++i) {
        uint32_t parent = fnv1a(index ^ i, int_mix[i % 16]) % rows;
        fnv_hash(int_mix, &cache_words[parent * HASH_WORDS], HASH_WORDS);
    }

    // Flatten and re-hash.
    for (int i = 0; i < HASH_WORDS; ++i) {
        store32(mix + 4 * i, int_mix[i]);
    }
    Keccak512(mix, HASH_BYTES, out);
}

void HashimotoLight(uint64_t cache_size, const std::vector<uint32_t>& cache_words,
                    const unsigned char header_hash[32], uint64_t nonce,
                    unsigned char digest_out[32], unsigned char result_out[32])
{
    const uint32_t rows = uint32_t(cache_size / MIX_BYTES);

    // seed = keccak512(header_hash || nonce as 8-byte LE)
    unsigned char seed_in[40];
    std::memcpy(seed_in, header_hash, 32);
    store32(seed_in + 32, uint32_t(nonce));
    store32(seed_in + 36, uint32_t(nonce >> 32));
    unsigned char seed[HASH_BYTES];
    Keccak512(seed_in, sizeof(seed_in), seed);
    const uint32_t seed_head = load32(seed);

    // start mix: replicate seed twice (64 bytes -> 128 bytes = 32 words)
    uint32_t mix[MIX_WORDS];
    for (int i = 0; i < MIX_WORDS; ++i) {
        mix[i] = load32(seed + 4 * (i % HASH_WORDS));
    }

    // fnv it with a lot of random cache nodes based on the seed.
    unsigned char item[HASH_BYTES];
    for (uint32_t i = 0; i < LOOP_ACCESSES; ++i) {
        uint32_t parent = fnv1a(i ^ seed_head, mix[i % MIX_WORDS]) % rows;
        // two consecutive dataset items (2 * 64 = 128 bytes = one mix row)
        uint32_t temp[HASH_WORDS * 2];
        GenerateDatasetItem(cache_words, 2 * parent, item);
        for (int k = 0; k < HASH_WORDS; ++k) temp[k] = load32(item + 4 * k);
        GenerateDatasetItem(cache_words, 2 * parent + 1, item);
        for (int k = 0; k < HASH_WORDS; ++k) temp[HASH_WORDS + k] = load32(item + 4 * k);
        fnv_hash(mix, temp, MIX_WORDS);
    }

    // Compress the mix into the digest.
    for (int i = 0; i < MIX_WORDS; i += 4) {
        mix[i / 4] = fnv1a(fnv1a(fnv1a(mix[i], mix[i + 1]), mix[i + 2]), mix[i + 3]);
    }
    for (int i = 0; i < 8; ++i) {
        store32(digest_out + 4 * i, mix[i]);
    }

    // result = keccak256(seed || digest) -- the full 64-byte seed, as in geth
    // hashimoto() (crypto.Keccak256(append(seed, digest...))).
    unsigned char result_in[HASH_BYTES + 32];
    std::memcpy(result_in, seed, HASH_BYTES);
    std::memcpy(result_in + HASH_BYTES, digest_out, 32);
    Keccak256(result_in, sizeof(result_in), result_out);
}

} // namespace etchash
