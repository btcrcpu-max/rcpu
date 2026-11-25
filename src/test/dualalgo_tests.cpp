// Copyright (c) 2024-2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <arith_uint256.h>
#include <chain.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <crypto/etchash.h>
#include <crypto/keccak.h>
#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <span.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <validation.h>

#include <array>

#include <memory>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(dualalgo_tests, BasicTestingSetup)

// !RCPU (dual-algo): the second-algorithm marker lives in nVersion bit 30,
// must not be confused with the versionbits deployment bits (0..28) and must
// survive the header copy used by miners. The historical constant name is
// kept for compatibility, but bit 30 now selects Etchash only.
BOOST_AUTO_TEST_CASE(block_version_bit30_flag)
{
    static_assert(BLOCK_VERSION_QHASH == 0x40000000, "second-algo marker is nVersion bit 30");
    BOOST_CHECK_EQUAL(BLOCK_VERSION_QHASH & 0x1fffffff, 0);

    CBlock block;
    block.SetNull();
    block.nVersion = 0x20000000; // versionbits top bits, no second-algo bit
    BOOST_CHECK(!(block.nVersion & BLOCK_VERSION_QHASH));
    block.nVersion |= BLOCK_VERSION_QHASH;
    CBlockHeader hdr = block.GetBlockHeader();
    BOOST_CHECK(hdr.nVersion & BLOCK_VERSION_QHASH);
    // Resetting the bit restores the original version.
    hdr.nVersion &= ~BLOCK_VERSION_QHASH;
    BOOST_CHECK_EQUAL(hdr.nVersion, 0x20000000);
}

// !RCPU (dual-algo etchash): original-Keccak (not SHA3) primitives must match
// the independent Python reference implementation (pycryptodome) vectors.
BOOST_AUTO_TEST_CASE(keccak_vectors)
{
    unsigned char out256[32];
    unsigned char out512[64];

    Keccak256(nullptr, 0, out256);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{out256, 32}),
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");

    Keccak256((const unsigned char*)"abc", 3, out256);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{out256, 32}),
        "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    Keccak512(nullptr, 0, out512);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{out512, 64}),
        "0eab42de4c3ceb9235fc91acffe746b29c29a8c366b7c60e4e67c466f36a4304c00f"
        "a9caf9d87976ba469bcbe06713b435f091ef2769fb160cdab33d3670680e");

    // Streaming Write/Finalize must match the one-shot form.
    CKeccak256 h;
    h.Write((const unsigned char*)"a", 1);
    h.Write((const unsigned char*)"bc", 2);
    h.Finalize(out256);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{out256, 32}),
        "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

// !RCPU (dual-algo etchash): cache sizes follow geth calcCacheSize (highest
// prime-bounded size below the linear threshold). Values cross-checked with
// the go-ethereum cache size table and the Python reference implementation.
BOOST_AUTO_TEST_CASE(etchash_cache_size_table)
{
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(0), 16776896);
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(1), 16907456);
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(2), 17039296);
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(3), 17170112);
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(4), 17301056);
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(100), 29882816);
    BOOST_CHECK_EQUAL(etchash::CalcCacheSize(1000), 147848768);
    // Sizes are always a multiple of 64 with a prime row count.
    for (uint64_t e : {0, 1, 2, 3, 4, 100, 1000}) {
        const uint64_t size = etchash::CalcCacheSize(e);
        BOOST_CHECK_EQUAL(size % 64, 0);
        BOOST_CHECK(etchash::IsPrime(size / 64));
    }
}

// !RCPU (dual-algo etchash): seed_e = keccak256^epoch(32 zero bytes), matching
// go-ethereum seedHash and the Python reference implementation.
BOOST_AUTO_TEST_CASE(etchash_seed_for_epoch)
{
    BOOST_CHECK_EQUAL(HexStr(etchash::SeedForEpoch(0)),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(HexStr(etchash::SeedForEpoch(1)),
        "290decd9548b62a8d60345a988386fc84ba6bc95484008f6362f93160ef3e563");
    BOOST_CHECK_EQUAL(HexStr(etchash::SeedForEpoch(2)),
        "510e4e770828ddbf7f7b00ab00a9f6adaf81c0dc9cc85f1f8249c256942d61d9");
    BOOST_CHECK_EQUAL(HexStr(etchash::SeedForEpoch(3)),
        "356e5a2cc1eba076e650ac7473fccc37952b46bc2e419a200cec0c451dce2336");
}

// !RCPU (dual-algo etchash): full cache generation for epoch 0 must reproduce
// the reference first word and last row (geth generateCache, cross-checked
// with the Python reference implementation).
BOOST_AUTO_TEST_CASE(etchash_cache_generation)
{
    const uint64_t size = etchash::CalcCacheSize(0);
    const std::vector<uint32_t> cache = etchash::GenerateCache(size, etchash::SeedForEpoch(0));
    BOOST_CHECK_EQUAL(cache.size(), size / 4);
    BOOST_CHECK_EQUAL(cache[0], 0x763e495eU);
    // Last 64-byte row as hex (little-endian words, plain byte order).
    unsigned char last_row[64];
    for (int i = 0; i < 16; ++i) {
        const uint32_t w = cache[cache.size() - 16 + i];
        last_row[4 * i + 0] = (unsigned char)(w);
        last_row[4 * i + 1] = (unsigned char)(w >> 8);
        last_row[4 * i + 2] = (unsigned char)(w >> 16);
        last_row[4 * i + 3] = (unsigned char)(w >> 24);
    }
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{last_row, 64}),
        "724f2f86c24c487809dc3897acbbd32d5d791e4536aa1520e65e93891a40dde5"
        "887899ffc556cbd174f426e32ae2ab711be859601c024d1514b29a27370b662e");
}

// !RCPU (dual-algo etchash): dataset (full DAG) sizes follow geth
// calcDatasetSize: highest prime-bounded size below
// DATASET_INIT_BYTES + DATASET_GROWTH_BYTES*epoch, prime on size/MIX_BYTES.
// Values are the first entries of go-ethereum's datasetSizes table.
BOOST_AUTO_TEST_CASE(etchash_dataset_size_table)
{
    BOOST_CHECK_EQUAL(etchash::CalcDatasetSize(0), 1073739904ULL);
    BOOST_CHECK_EQUAL(etchash::CalcDatasetSize(1), 1082130304ULL);
    BOOST_CHECK_EQUAL(etchash::CalcDatasetSize(2), 1090514816ULL);
    BOOST_CHECK_EQUAL(etchash::CalcDatasetSize(3), 1098906752ULL);
    BOOST_CHECK_EQUAL(etchash::CalcDatasetSize(4), 1107293056ULL);
    for (uint64_t e : {0, 1, 2, 3, 4}) {
        const uint64_t size = etchash::CalcDatasetSize(e);
        BOOST_CHECK_EQUAL(size % etchash::MIX_BYTES, 0);
        BOOST_CHECK(etchash::IsPrime(size / etchash::MIX_BYTES));
    }
}

// !RCPU (dual-algo etchash): official go-ethereum TestHashimoto vector
// (algorithm_test.go). Small 1 KiB cache and 32 KiB dataset keep the test fast
// while exercising the dataset-size-derived row count end to end.
BOOST_AUTO_TEST_CASE(etchash_hashimoto_geth_vector)
{
    const std::vector<uint32_t> cache = etchash::GenerateCache(1024, etchash::SeedForEpoch(0));
    const unsigned char hash[32] = {
        0xc9,0x14,0x9c,0xc0,0x38,0x6e,0x68,0x9d,0x78,0x9a,0x1c,0x2f,0x3d,0x5d,0x16,0x9a,
        0x61,0xa6,0x21,0x8e,0xd3,0x0e,0x74,0x41,0x4d,0xc7,0x36,0xe4,0x42,0xef,0x3d,0x1f};
    unsigned char digest[32], result[32];
    etchash::HashimotoLight(32 * 1024, cache, hash, 0, digest, result);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{digest, 32}),
        "e4073cffaef931d37117cefd9afd27ea0f1cad6a981dd2605c4a1ac97c519800");
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{result, 32}),
        "d3539235ee2e6f8db665c0a72169f55b7f6c605712330b778ec3944f0eb5a557");
}

// !RCPU (dual-algo etchash): hashimotoLight over the epoch-0 and epoch-1
// caches with header_hash = keccak256("rcpu etchash vector"), nonce 42 --
// hardcoded vectors from the independent Python reference implementation so a
// porting error cannot silently pass.
BOOST_AUTO_TEST_CASE(etchash_hashimoto_vectors)
{
    unsigned char header_hash[32];
    Keccak256((const unsigned char*)"rcpu etchash vector", 19, header_hash);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{header_hash, 32}),
        "c281cdbb7f3e1756bf84608387e362c68c56daab2059752210f8ba14dd475e65");

    const uint64_t nonce = 42;
    unsigned char digest[32], result[32];

    {
        const uint64_t cache_size = etchash::CalcCacheSize(0);
        const uint64_t dataset_size = etchash::CalcDatasetSize(0);
        const std::vector<uint32_t> cache = etchash::GenerateCache(cache_size, etchash::SeedForEpoch(0));
        // hashimotoLight's row count is dataset_size / MIX_BYTES (full DAG),
        // NOT cache_size / MIX_BYTES (the pre-fix bug).
        etchash::HashimotoLight(dataset_size, cache, header_hash, nonce, digest, result);
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{digest, 32}),
            "bfbb3b1a38793e4076f16a176d4cad618ecc6362ea236a82c9140402a95f10d0");
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{result, 32}),
            "e0e36718f1c01cd5edc80ede160531f8b3756390f8353ae9da9b635ccddd4f41");
    }

    {
        const uint64_t cache_size = etchash::CalcCacheSize(1);
        const uint64_t dataset_size = etchash::CalcDatasetSize(1);
        const std::vector<uint32_t> cache = etchash::GenerateCache(cache_size, etchash::SeedForEpoch(1));
        etchash::HashimotoLight(dataset_size, cache, header_hash, nonce, digest, result);
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{digest, 32}),
            "3120a41699eac1d2a0896914b37f287946dca28ae8397568e622ffe02321d709");
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{result, 32}),
            "799bdf1e383301e774b1937b50e9326392d2359d1638aa40bad22694d91fbdb8");
    }
}

// !RCPU (dual-algo etchash): the 32-byte header hash fed to hashimoto must NOT
// depend on the nonce (standard Ethash/Etchash semantics). This is what lets a
// stock GPU kernel — which keeps the header hash constant and only varies the
// nonce — mine RCPU Etchash. Regression guard: if the nonce leaked back into
// the hashed header, every off-the-shelf GPU miner would produce invalid
// shares while CPU miners using the same code would still agree with each
// other, i.e. the breakage would be silent.
BOOST_AUTO_TEST_CASE(etchash_header_hash_is_nonce_independent)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;

    params.nEtchashBigEndianHeight = 0;

    CBlockHeader block;
    block.nVersion = 1 | BLOCK_VERSION_QHASH;
    block.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    block.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
    block.nTime = 1710000000;
    block.nBits = 0x207fffff;
    block.nNonce = 123456;

    uint256 produced;
    BOOST_CHECK(CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_MINING, &produced));

    const uint64_t dataset_size = etchash::CalcDatasetSize(0);
    const std::vector<uint32_t> cache = etchash::GenerateCache(etchash::CalcCacheSize(0), etchash::SeedForEpoch(0));
    unsigned char digest[32], result[32], old_result[32];

    // Standard domain: nonce zeroed inside the header hash, nonce passed separately.
    {
        CBlockHeader zeroed = block;
        zeroed.nNonce = 0;
        const DataStream input = SerializeRandomXHeader(zeroed);
        unsigned char header_hash[32];
        Keccak256((const unsigned char*)input.data(), input.size(), header_hash);
        etchash::HashimotoLight(dataset_size, cache, header_hash, uint64_t(block.nNonce), digest, result);

        uint256 expected;
        std::memcpy(expected.begin(), result, 32);
        BOOST_CHECK(produced == expected);
    }

    // The pre-change domain (nonce part of the header hash) must NOT reproduce
    // it, otherwise this test would pass even with the old, non-standard rule.
    {
        const DataStream input = SerializeRandomXHeader(block);
        unsigned char header_hash[32];
        Keccak256((const unsigned char*)input.data(), input.size(), header_hash);
        etchash::HashimotoLight(dataset_size, cache, header_hash, uint64_t(block.nNonce), digest, old_result);

        uint256 old_expected;
        std::memcpy(old_expected.begin(), old_result, 32);
        BOOST_CHECK(produced != old_expected);
    }
}

// !RCPU (dual-algo etchash): end-to-end etchash proof-of-work semantics —
// epoch plumbing, solve/verify roundtrip, tamper rejection, pre-activation
// refusal, and epoch-switch rejection.
BOOST_AUTO_TEST_CASE(etchash_pow_roundtrip)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;

    // Epoch plumbing: epoch = (height - activation) / epochLength.
    BOOST_CHECK_EQUAL(GetEtchashEpoch(149, params), -1);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(150, params), 0);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(159, params), 0);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(160, params), 1);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(-1, params), -1);
    BOOST_CHECK(GetEtchashSeedHash(150, params) == etchash::SeedForEpoch(0));
    BOOST_CHECK(GetEtchashSeedHash(160, params) == etchash::SeedForEpoch(1));
    BOOST_CHECK(GetEtchashSeedHash(149, params).IsNull());

    CBlockHeader block;
    block.nVersion = 1 | BLOCK_VERSION_QHASH;
    block.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    block.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
    block.nTime = 1710000000;
    block.nBits = 0x207fffff;
    block.nNonce = 0;

    // Solve at height 150 (epoch 0) by incrementing the nonce.
    uint256 solved;
    bool found = false;
    for (uint32_t nonce = 0; nonce < 1000; ++nonce) {
        block.nNonce = nonce;
        if (CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_MINING, &solved)) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
    block.hashRandomX = solved;

    // Full verification accepts the solved header.
    BOOST_CHECK(CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_FULL));

    // Tampering with the commitment field must fail.
    CBlockHeader tampered = block;
    *tampered.hashRandomX.begin() ^= 0x01;
    BOOST_CHECK(!CheckProofOfWorkEtchash(tampered, 150, params, POW_VERIFY_FULL));

    // Pre-activation height: etchash verification must refuse outright.
    BOOST_CHECK(!CheckProofOfWorkEtchash(block, 149, params, POW_VERIFY_FULL));

    // Epoch switch: a header solved against the epoch-0 cache must fail at
    // height 160, where epoch 1 uses a different seed and cache.
    BOOST_CHECK(!CheckProofOfWorkEtchash(block, 160, params, POW_VERIFY_FULL));

    // Genesis-style headers (null prev) short-circuit regardless of content.
    CBlockHeader genesis_like = block;
    genesis_like.hashPrevBlock.SetNull();
    genesis_like.hashRandomX.SetNull();
    uint256 out;
    BOOST_CHECK(CheckProofOfWorkEtchash(genesis_like, 150, params, POW_VERIFY_FULL, &out));
    BOOST_CHECK(out.IsNull());
}

// P2-7: standard Etchash compares the hashimoto result as a big-endian number.
// The historical RCPU code copied the bytes straight into uint256 (little-endian
// interpretation), so geth/ethminer nonces were rejected as high-hash.
BOOST_AUTO_TEST_CASE(etchash_big_endian_result_order)
{
    const std::vector<unsigned char> raw = ParseHex(
        "d3539235ee2e6f8db665c0a72169f55b7f6c605712330b778ec3944f0eb5a557");
    BOOST_REQUIRE_EQUAL(raw.size(), 32U);

    uint256 historical;
    std::memcpy(historical.begin(), raw.data(), 32);

    uint256 big_endian;
    std::reverse_copy(raw.begin(), raw.end(), big_endian.begin());

    arith_uint256 expected;
    expected.SetHex("d3539235ee2e6f8db665c0a72169f55b7f6c605712330b778ec3944f0eb5a557");
    BOOST_CHECK(UintToArith256(big_endian) == expected);
    BOOST_CHECK(UintToArith256(historical) != expected);
}

// P2-7: the height-gated big-endian path must solve and verify round-trip.
BOOST_AUTO_TEST_CASE(etchash_big_endian_pow_roundtrip)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;

    params.nEtchashBigEndianHeight = 150;

    CBlockHeader block;
    block.nVersion = 1 | BLOCK_VERSION_QHASH;
    block.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    block.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
    block.nTime = 1710000000;
    block.nBits = 0x207fffff;

    uint256 solved;
    bool found = false;
    for (uint32_t nonce = 0; nonce < 1000; ++nonce) {
        block.nNonce = nonce;
        if (CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_MINING, &solved)) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
    block.hashRandomX = solved;
    BOOST_CHECK(CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_FULL));

    CBlockHeader tampered = block;
    *tampered.hashRandomX.begin() ^= 0x01;
    BOOST_CHECK(!CheckProofOfWorkEtchash(tampered, 150, params, POW_VERIFY_FULL));
}

// !RCPU (dual-algo etchash): regression for the network-layer headers pre-check.
// HasValidProofOfWork() used to probe the wrong algorithm for bit-30 headers,
// so every relayed Etchash header looked like invalid PoW and cost the peer 100
// misbehaviour points (disconnect). It must verify Etchash when the height is
// known and defer (not reject) when the height is unknown.
BOOST_AUTO_TEST_CASE(etchash_header_pow_precheck_is_height_aware)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;

    CBlockHeader header;
    header.nVersion = 1 | BLOCK_VERSION_QHASH;
    header.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    header.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
    header.nTime = 1710000000;
    header.nBits = 0x207fffff;

    static const auto mine_header = [](CBlockHeader& h, int height, const Consensus::Params& p) {
        uint256 solved;
        for (uint32_t nonce = 0; nonce < 100000; ++nonce) {
            h.nNonce = nonce;
            if (CheckProofOfWorkEtchash(h, height, p, POW_VERIFY_MINING, &solved)) {
                h.hashRandomX = solved;
                return true;
            }
        }
        return false;
    };

    BOOST_REQUIRE(mine_header(header, 150, params));

    // Pre-activation fix this returned false: the old probe did not match an
    // Etchash result and the RandomX commitment is unrelated.
    BOOST_CHECK(HasValidProofOfWork({header}, params, /*nStartHeight=*/150));

    // Height unknown: deferred to AcceptBlockHeader, must not be rejected here.
    BOOST_CHECK(HasValidProofOfWork({header}, params, /*nStartHeight=*/-1));

    // !RCPU (dual-algo etchash security): a tampered claim that merely
    // satisfies the target must NOT pass the header pre-check. Etchash has no
    // header-derived commitment (unlike RandomX/QHash where hashRandomX is
    // recomputed from the header), so a target-only pre-check is forgeable:
    // a peer can pick an arbitrary hashRandomX just under target. The pre-check
    // therefore runs POW_VERIFY_FULL for Etchash, so the forgery is rejected
    // before it ever reaches cs_main / AcceptBlockHeader.
    CBlockHeader tampered = header;
    *tampered.hashRandomX.begin() ^= 0x01;
    BOOST_CHECK(!HasValidProofOfWork({tampered}, params, /*nStartHeight=*/150));
    BOOST_CHECK(!CheckProofOfWorkEtchash(tampered, 150, params, POW_VERIFY_FULL));

    // Over-target garbage is still rejected by the pre-check itself.
    CBlockHeader garbage = header;
    garbage.hashRandomX = uint256S("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    BOOST_CHECK(!HasValidProofOfWork({garbage}, params, /*nStartHeight=*/150));
}

// !RCPU (dual-algo etchash): POW_VERIFY_COMMITMENT_ONLY must be the cheap
// target-only pre-check (no hashimoto), so CheckBlock plus the headers
// pre-check no longer duplicate the full hashimoto run that AcceptBlockHeader
// performs with POW_VERIFY_FULL.
BOOST_AUTO_TEST_CASE(etchash_commitment_only_is_target_check)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;

    params.nEtchashBigEndianHeight = 150; // big-endian comparison active

    CBlockHeader block;
    block.nVersion = 1 | BLOCK_VERSION_QHASH;
    block.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    block.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
    block.nTime = 1710000000;
    block.nBits = 0x207fffff;

    uint256 solved;
    bool found = false;
    for (uint32_t nonce = 0; nonce < 1000; ++nonce) {
        block.nNonce = nonce;
        if (CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_MINING, &solved)) {
            found = true;
            break;
        }
    }
    BOOST_REQUIRE(found);
    block.hashRandomX = solved;

    // The real solution passes both the cheap pre-check and full verification.
    BOOST_CHECK(CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_COMMITMENT_ONLY));
    BOOST_CHECK(CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_FULL));

    // A fabricated claim that satisfies the target but is not the hashimoto
    // result passes the cheap pre-check (big-endian value 1) yet fails full
    // verification: that split is what removes the duplicate hashimoto run.
    CBlockHeader forged = block;
    forged.hashRandomX.SetNull();
    *forged.hashRandomX.begin() = 0x01;
    BOOST_CHECK(CheckProofOfWorkEtchash(forged, 150, params, POW_VERIFY_COMMITMENT_ONLY));
    BOOST_CHECK(!CheckProofOfWorkEtchash(forged, 150, params, POW_VERIFY_FULL));

    // A null claim and an over-target claim are both rejected by the pre-check.
    CBlockHeader null_claim = block;
    null_claim.hashRandomX.SetNull();
    BOOST_CHECK(!CheckProofOfWorkEtchash(null_claim, 150, params, POW_VERIFY_COMMITMENT_ONLY));
    CBlockHeader over_target = block;
    over_target.hashRandomX = uint256S("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    BOOST_CHECK(!CheckProofOfWorkEtchash(over_target, 150, params, POW_VERIFY_COMMITMENT_ONLY));

    // The legacy little-endian branch (pre-nEtchashBigEndianHeight semantics)
    // compares the stored uint256 directly.
    Consensus::Params params_le = params;
    params_le.nEtchashBigEndianHeight = std::numeric_limits<int>::max();
    BOOST_CHECK(CheckProofOfWorkEtchash(forged, 150, params_le, POW_VERIFY_COMMITMENT_ONLY));
    BOOST_CHECK(!CheckProofOfWorkEtchash(over_target, 150, params_le, POW_VERIFY_COMMITMENT_ONLY));

    // Mining mode still computes the full hashimoto and exposes the result.
    uint256 mined;
    BOOST_CHECK(CheckProofOfWorkEtchash(block, 150, params, POW_VERIFY_MINING, &mined));
    BOOST_CHECK(mined == block.hashRandomX);
}

// !RCPU (dual-algo etchash): a valid Etchash block whose parent is unknown must
// not be rejected as "high-hash" by the context-free CheckBlock(). Before the
// fix the old probe rejected it, which marked the relaying peer as misbehaving
// (BLOCK_INVALID_HEADER) for ordinary out-of-order relay.
BOOST_AUTO_TEST_CASE(etchash_block_unknown_height_defers_pow)
{
    // CheckBlock() takes the dual-algo branch only when g_isRandomX is set
    // (node runtime); unit tests default to false.
    struct RandomXFlagGuard {
        bool saved{g_isRandomX.load()};
        RandomXFlagGuard() { g_isRandomX = true; }
        ~RandomXFlagGuard() { g_isRandomX = saved; }
    } guard;

    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;
    params.fPowRandomX = true;

    CBlock block;
    block.nVersion = 1 | BLOCK_VERSION_QHASH;
    block.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    block.nTime = 1710000000;
    block.nBits = 0x207fffff;

    CMutableTransaction coinbase;
    coinbase.nVersion = 1;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    CTxOut out;
    out.nValue.SetToAmount(50 * COIN);
    out.scriptPubKey = CScript() << OP_TRUE;
    coinbase.vout.push_back(out);
    block.vtx.push_back(MakeTransactionRef(coinbase));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // Solve at height 150. Retry with a different timestamp if the unrelated
    // RandomX commitment happens to pass the target, so the old buggy path
    // (reject as high-hash) is guaranteed to reproduce on a pre-fix build.
    bool solved = false;
    for (int attempt = 0; attempt < 64 && !solved; ++attempt) {
        CBlockHeader candidate = block.GetBlockHeader();
        uint256 etchash_result;
        bool mined = false;
        for (uint32_t nonce = 0; nonce < 100000; ++nonce) {
            candidate.nNonce = nonce;
            if (CheckProofOfWorkEtchash(candidate, 150, params, POW_VERIFY_MINING, &etchash_result)) {
                candidate.hashRandomX = etchash_result;
                mined = true;
                break;
            }
        }
        BOOST_REQUIRE(mined);
        if (!CheckProofOfWorkRandomX(candidate, params, POW_VERIFY_COMMITMENT_ONLY)) {
            block.nNonce = candidate.nNonce;
            block.hashRandomX = candidate.hashRandomX;
            solved = true;
        } else {
            block.nTime += 1;
        }
    }
    BOOST_REQUIRE(solved);

    // Height unknown (-1): PoW verdict is deferred to AcceptBlockHeader.
    BlockValidationState state;
    BOOST_CHECK(CheckBlock(block, state, params, /*fCheckPOW=*/true, /*fCheckMerkleRoot=*/true, /*nHeight=*/-1));
}

// !RCPU (dual-algo count cache): the cumulative bit-30 counter must be built
// from the parent and must stay 0 for pre-activation blocks even when the
// version bit is set.
BOOST_AUTO_TEST_CASE(second_algo_count_cache_tracks_bit30)
{
    const int nActivation = 150;
    std::vector<std::unique_ptr<CBlockIndex>> chain;
    int64_t expected = 0;

    for (int height = 148; height <= 158; ++height) {
        auto index = std::make_unique<CBlockIndex>();
        index->nHeight = height;
        index->pprev = chain.empty() ? nullptr : chain.back().get();
        // Even heights carry the second-algorithm bit, including pre-activation
        // heights 148 and 150/152/... (the pre-activation ones must not count).
        index->nVersion = (height % 2 == 0) ? BLOCK_VERSION_QHASH : 0;
        if (height >= nActivation && (index->nVersion & BLOCK_VERSION_QHASH)) ++expected;

        index->nSecondAlgoCount = ComputeSecondAlgoCount(*index, nActivation);
        BOOST_CHECK_EQUAL(index->nSecondAlgoCount, expected);
        chain.push_back(std::move(index));
    }
    BOOST_CHECK_EQUAL(chain.front()->nSecondAlgoCount, 0);
    BOOST_CHECK_EQUAL(chain.back()->nSecondAlgoCount, 5); // 150,152,154,156,158

    // An index whose parent count is unknown propagates -1 so the difficulty
    // path falls back to the chain walk instead of using a bogus 0.
    auto unset_parent = std::make_unique<CBlockIndex>();
    unset_parent->nHeight = nActivation;
    auto child = std::make_unique<CBlockIndex>();
    child->nHeight = nActivation + 1;
    child->pprev = unset_parent.get();
    BOOST_CHECK_EQUAL(ComputeSecondAlgoCount(*child, nActivation), -1);
}

// !RCPU (dual-algo count cache): cached and walked difficulty must be
// bit-for-bit identical for both algorithms, at and after activation.
BOOST_AUTO_TEST_CASE(dual_algo_difficulty_cache_matches_chain_walk)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nDualAlgoTargetSpacing = 600;
    params.nPowTargetSpacing = 300;
    params.nEtchashEpochLength = 10;
    params.fPowAllowMinDifficultyBlocks = false;
    params.fPowNoRetargeting = false;
    params.nASERTHalfLife = 2 * 24 * 60 * 60;

    std::vector<std::unique_ptr<CBlockIndex>> chain;
    for (int height = 140; height <= 185; ++height) {
        auto index = std::make_unique<CBlockIndex>();
        index->nHeight = height;
        index->pprev = chain.empty() ? nullptr : chain.back().get();
        index->nTime = 1710000000 + (height - 140) * 300;
        index->nBits = 0x207fffff;
        index->nVersion = (height % 3 == 0) ? BLOCK_VERSION_QHASH : 0;
        // No BuildSkip(): this synthetic chain starts at height 140, so the
        // skip pointers (which assume an unbroken chain from genesis) would
        // point below the first index. GetAncestor() walks pprev instead.
        index->nChainWork = (index->pprev ? index->pprev->nChainWork : arith_uint256(0)) + GetBlockProof(*index);
        index->nSecondAlgoCount = ComputeSecondAlgoCount(*index, params.nDualAlgoActivationHeight);
        chain.push_back(std::move(index));
    }

    int64_t manual = 0;
    for (const auto& index : chain) {
        if (index->nHeight >= params.nDualAlgoActivationHeight && (index->nVersion & BLOCK_VERSION_QHASH)) ++manual;
    }
    CBlockIndex* tip = chain.back().get();
    BOOST_CHECK_EQUAL(tip->nSecondAlgoCount, manual);

    CBlockHeader header;
    header.nTime = tip->nTime + 300;

    header.nVersion = 0;
    const uint32_t randomx_cached = GetNextWorkRequired(tip, &header, params);
    header.nVersion = BLOCK_VERSION_QHASH;
    const uint32_t etchash_cached = GetNextWorkRequired(tip, &header, params);

    // Force the O(N) fallback by clearing the cached count; the result must be
    // identical to the O(1) cached path.
    tip->nSecondAlgoCount = -1;

    header.nVersion = 0;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(tip, &header, params), randomx_cached);
    header.nVersion = BLOCK_VERSION_QHASH;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(tip, &header, params), etchash_cached);

    // Sanity: the two algorithms do not generally produce the same target.
    BOOST_CHECK(randomx_cached != etchash_cached);
}

// !RCPU (H-1): an algorithm that has been idle for longer than
// nDualAlgoReanchorGap must not release the banked lateness as powLimit
// blocks. The resume block may drop by at most nDualAlgoMaxDrop; the next
// block anchors on the resume block, so the drop is not carried forward.
BOOST_AUTO_TEST_CASE(dual_algo_reanchor_discards_banked_lateness)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nDualAlgoTargetSpacing = 600;
    params.nPowTargetSpacing = 300;
    params.nASERTHalfLife = 2 * 24 * 60 * 60;

    params.nEtchashEpochLength = 10;
    params.fPowAllowMinDifficultyBlocks = false;
    params.fPowNoRetargeting = false;
    params.nDualAlgoLatenessFixHeight = 160;
    params.nDualAlgoReanchorGap = 12 * 60 * 60;
    params.nDualAlgoMaxDrop = 8;

    std::vector<std::unique_ptr<CBlockIndex>> chain;
    const int64_t nBaseTime{1710000000};
    for (int height = 140; height <= 159; ++height) {
        auto index = std::make_unique<CBlockIndex>();
        index->nHeight = height;
        index->pprev = chain.empty() ? nullptr : chain.back().get();
        index->nTime = nBaseTime + (height - 140) * 300;
        index->nBits = 0x1f00ffff;
        // One Etchash block at activation, then RandomX only: the Etchash
        // branch is idle for the whole 60-day interval below.
        index->nVersion = (height == 150) ? BLOCK_VERSION_QHASH : 0;
        index->nChainWork = (index->pprev ? index->pprev->nChainWork : arith_uint256(0)) + GetBlockProof(*index);
        index->nSecondAlgoCount = ComputeSecondAlgoCount(*index, params.nDualAlgoActivationHeight);
        UpdateDualAlgoState(*index, params);
        chain.push_back(std::move(index));
    }

    // Jump the tip's timestamp forward by 60 days: with a 2-day half-life the
    // old formula releases this as powLimit, far above the 12h threshold.
    chain.back()->nTime = nBaseTime + 60 * 24 * 60 * 60;
    CBlockIndex* tip = chain.back().get();
    BOOST_CHECK_EQUAL(tip->nLastEtchashHeight, 150);
    BOOST_CHECK_EQUAL(tip->nLastRandomXHeight, 159);
    BOOST_CHECK_EQUAL(tip->nAsertAnchorEtchashHeight, 149);

    CBlockHeader header;
    header.nTime = tip->nTime + 600;
    header.nVersion = BLOCK_VERSION_QHASH;

    // Old rule (no fix active): the 60-day bank is released at powLimit.
    params.nDualAlgoLatenessFixHeight = std::numeric_limits<int>::max();
    const uint32_t old_bits{GetNextWorkRequired(tip, &header, params)};
    BOOST_CHECK_EQUAL(old_bits, UintToArith256(params.powLimit).GetCompact());

    // New rule: re-anchor to the last Etchash block and cap the drop at 8x.
    params.nDualAlgoLatenessFixHeight = 160;
    const uint32_t resume_bits{GetNextWorkRequired(tip, &header, params)};
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1f00ffff);
    const arith_uint256 maxDrop{params.nDualAlgoMaxDrop};
    const arith_uint256 expected = refTarget * maxDrop;
    BOOST_CHECK_EQUAL(resume_bits, expected.GetCompact());
    BOOST_CHECK(resume_bits != UintToArith256(params.powLimit).GetCompact());

    // The resume block becomes the new anchor. The next Etchash block must
    // not fall back to the activation anchor (which would re-release the bank).
    auto resume = std::make_unique<CBlockIndex>();
    resume->nHeight = 160;
    resume->pprev = tip;
    resume->nTime = header.nTime;
    resume->nBits = resume_bits;
    resume->nVersion = BLOCK_VERSION_QHASH;
    resume->nChainWork = tip->nChainWork + GetBlockProof(*resume);
    resume->nSecondAlgoCount = ComputeSecondAlgoCount(*resume, params.nDualAlgoActivationHeight);
    UpdateDualAlgoState(*resume, params);
    BOOST_CHECK_EQUAL(resume->nLastEtchashHeight, 160);
    BOOST_CHECK_EQUAL(resume->nAsertAnchorEtchashHeight, 160);

    CBlockHeader next_header;
    next_header.nTime = resume->nTime + 600;
    next_header.nVersion = BLOCK_VERSION_QHASH;
    const uint32_t next_bits{GetNextWorkRequired(resume.get(), &next_header, params)};
    BOOST_CHECK(next_bits != UintToArith256(params.powLimit).GetCompact());
    BOOST_CHECK(next_bits <= resume_bits);
}

namespace {
/** Append one synthetic block index and return it.
 *
 *  Mirrors BlockManager::AddToBlockIndex(): height/pprev/chainwork come from the
 *  given parent, then the dual-algo count cache and the H-1 re-anchor state are
 *  derived exactly as the node derives them when it indexes a block.
 */
CBlockIndex* AppendDualAlgoIndex(std::vector<std::unique_ptr<CBlockIndex>>& owned,
                                 CBlockIndex* pprev,
                                 const Consensus::Params& params,
                                 int32_t height,
                                 int32_t version,
                                 int64_t time,
                                 uint32_t bits)
{
    auto index = std::make_unique<CBlockIndex>();
    index->nHeight = height;
    index->pprev = pprev;
    index->nTime = time;
    index->nBits = bits;
    index->nVersion = version;
    index->nChainWork = (pprev ? pprev->nChainWork : arith_uint256(0)) + GetBlockProof(*index);
    index->nSecondAlgoCount = ComputeSecondAlgoCount(*index, params.nDualAlgoActivationHeight);
    UpdateDualAlgoState(*index, params);
    owned.push_back(std::move(index));
    return owned.back().get();
}

/** Parameters around the H-1 fix: activation 150, fix 160, 12h gap, 8x cap. */
Consensus::Params FixHeightParams()
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nDualAlgoTargetSpacing = 600;
    params.nPowTargetSpacing = 300;
    params.nASERTHalfLife = 2 * 24 * 60 * 60;
    params.nEtchashEpochLength = 10;
    params.fPowAllowMinDifficultyBlocks = false;
    params.fPowNoRetargeting = false;
    params.nDualAlgoLatenessFixHeight = 160;
    params.nDualAlgoReanchorGap = 12 * 60 * 60;
    params.nDualAlgoMaxDrop = 8;
    return params;
}
} // namespace

// !RCPU (H-1, reorg): the re-anchor state is stored per block index and must be
// derived only from that index's own ancestry. Reorganising across the fix
// height must not let the abandoned branch's anchor leak into the new branch,
// and must not mutate the abandoned branch either: a header-only reorg rebuilds
// the new branch through AddToBlockIndex while the old indexes still exist.
BOOST_AUTO_TEST_CASE(dual_algo_reorg_across_fix_height_keeps_branch_state)
{
    const Consensus::Params params{FixHeightParams()};
    const int64_t nBaseTime{1710000000};

    std::vector<std::unique_ptr<CBlockIndex>> chain;
    for (int32_t height = 140; height <= 159; ++height) {
        const int32_t version = (height == 150) ? BLOCK_VERSION_QHASH : 0;
        AppendDualAlgoIndex(chain, chain.empty() ? nullptr : chain.back().get(), params,
                            height, version, nBaseTime + (height - 140) * 300, 0x1f00ffff);
    }
    CBlockIndex* fork_point = chain.back().get();

    // More than 12h after the common tip, measured from the last Etchash block
    // at height 150.
    const int64_t nResumeTime{nBaseTime + (159 - 140) * 300 + 13 * 60 * 60};

    // Branch A (abandoned): Etchash resumes, so it becomes its own anchor.
    CBlockHeader header_a;
    header_a.nTime = nResumeTime;
    header_a.nVersion = BLOCK_VERSION_QHASH;
    const uint32_t bits_a{GetNextWorkRequired(fork_point, &header_a, params)};

    std::vector<std::unique_ptr<CBlockIndex>> branch_a;
    CBlockIndex* a160 = AppendDualAlgoIndex(branch_a, fork_point, params,
                                            160, BLOCK_VERSION_QHASH, nResumeTime, bits_a);
    BOOST_CHECK_EQUAL(a160->nLastEtchashHeight, 160);
    // Re-anchor check uses pprev's time vs. last-same time, not this block's
    // time; with the 45 min gap between height 150 (Etchash) and 159
    // (pprev) no re-anchor fires, so the activation anchor stays.
    BOOST_CHECK_EQUAL(a160->nAsertAnchorEtchashHeight, 149);

    // Branch B (selected): a RandomX block at the same height, then Etchash.
    CBlockHeader header_b;
    header_b.nTime = nResumeTime;
    header_b.nVersion = 0;
    const uint32_t bits_b160{GetNextWorkRequired(fork_point, &header_b, params)};

    std::vector<std::unique_ptr<CBlockIndex>> branch_b;
    CBlockIndex* b160 = AppendDualAlgoIndex(branch_b, fork_point, params,
                                            160, 0, nResumeTime, bits_b160);
    // The new branch starts from the common ancestor's state, not from A's.
    BOOST_CHECK_EQUAL(b160->nLastEtchashHeight, 150);
    BOOST_CHECK_EQUAL(b160->nAsertAnchorEtchashHeight, 149);
    BOOST_CHECK_EQUAL(b160->nLastRandomXHeight, 160);
    BOOST_CHECK_EQUAL(b160->nAsertAnchorRandomXHeight, 149);

    CBlockHeader header_b161;
    header_b161.nTime = nResumeTime + 600;
    header_b161.nVersion = BLOCK_VERSION_QHASH;
    const uint32_t bits_b161{GetNextWorkRequired(b160, &header_b161, params)};
    CBlockIndex* b161 = AppendDualAlgoIndex(branch_b, b160, params,
                                            161, BLOCK_VERSION_QHASH, header_b161.nTime, bits_b161);
    BOOST_CHECK_EQUAL(b161->nLastEtchashHeight, 161);
    BOOST_CHECK_EQUAL(b161->nAsertAnchorEtchashHeight, 161);

    // Building branch B must not touch branch A's indexes.
    BOOST_CHECK_EQUAL(a160->nLastEtchashHeight, 160);
    BOOST_CHECK_EQUAL(a160->nAsertAnchorEtchashHeight, 149);

    // Isolated reference chain with the same ancestry and timestamps: the
    // difficulty on the selected branch must be bit-for-bit identical.
    std::vector<std::unique_ptr<CBlockIndex>> reference;
    for (int32_t height = 140; height <= 159; ++height) {
        const int32_t version = (height == 150) ? BLOCK_VERSION_QHASH : 0;
        AppendDualAlgoIndex(reference, reference.empty() ? nullptr : reference.back().get(), params,
                            height, version, nBaseTime + (height - 140) * 300, 0x1f00ffff);
    }
    CBlockIndex* r160 = AppendDualAlgoIndex(reference, reference.back().get(), params,
                                            160, 0, nResumeTime, bits_b160);
    CBlockHeader ref_header;
    ref_header.nTime = nResumeTime + 600;
    ref_header.nVersion = BLOCK_VERSION_QHASH;
    BOOST_CHECK_EQUAL(bits_b161, GetNextWorkRequired(r160, &ref_header, params));
}

// !RCPU (H-1, restart/reindex): every per-algorithm field is memory-only and is
// rebuilt by BlockManager::LoadBlockIndex() in height order. Rebuilding from
// headers alone must reproduce the incremental AddToBlockIndex() state exactly,
// otherwise a node would change its own difficulty after a restart or -reindex.
BOOST_AUTO_TEST_CASE(dual_algo_state_rebuild_matches_incremental)
{
    const Consensus::Params params{FixHeightParams()};
    const int64_t nBaseTime{1710000000};

    std::vector<std::unique_ptr<CBlockIndex>> chain;
    for (int32_t height = 140; height <= 159; ++height) {
        const int32_t version = (height == 150) ? BLOCK_VERSION_QHASH : 0;
        AppendDualAlgoIndex(chain, chain.empty() ? nullptr : chain.back().get(), params,
                            height, version, nBaseTime + (height - 140) * 300, 0x1f00ffff);
    }

    // 60 days idle for Etchash: the height-160 resume block must re-anchor.
    chain.back()->nTime += 60 * 24 * 60 * 60;
    int64_t nTime{chain.back()->nTime + 600};
    for (int32_t height = 160; height <= 164; ++height) {
        const int32_t version = (height == 160) ? BLOCK_VERSION_QHASH : 0;
        CBlockHeader header;
        header.nTime = nTime;
        header.nVersion = version;
        const uint32_t bits{GetNextWorkRequired(chain.back().get(), &header, params)};
        AppendDualAlgoIndex(chain, chain.back().get(), params, height, version, nTime, bits);
        nTime += 600;
    }

    struct Snapshot {
        int64_t second_algo_count;
        int32_t last_randomx;
        int32_t last_etchash;
        int32_t anchor_randomx;
        int32_t anchor_etchash;
    };
    std::vector<Snapshot> incremental;
    for (const auto& index : chain) {
        incremental.push_back({index->nSecondAlgoCount, index->nLastRandomXHeight, index->nLastEtchashHeight,
                               index->nAsertAnchorRandomXHeight, index->nAsertAnchorEtchashHeight});
    }

    // Simulate a fresh process: the memory-only fields start at their defaults,
    // then LoadBlockIndex() recomputes them in height order.
    for (auto& index : chain) {
        index->nSecondAlgoCount = -1;
        index->nLastRandomXHeight = -1;
        index->nLastEtchashHeight = -1;
        index->nAsertAnchorRandomXHeight = -1;
        index->nAsertAnchorEtchashHeight = -1;
    }
    for (auto& index : chain) {
        index->nSecondAlgoCount = ComputeSecondAlgoCount(*index, params.nDualAlgoActivationHeight);
        UpdateDualAlgoState(*index, params);
    }

    for (size_t i = 0; i < chain.size(); ++i) {
        BOOST_CHECK_EQUAL(chain[i]->nSecondAlgoCount, incremental[i].second_algo_count);
        BOOST_CHECK_EQUAL(chain[i]->nLastRandomXHeight, incremental[i].last_randomx);
        BOOST_CHECK_EQUAL(chain[i]->nLastEtchashHeight, incremental[i].last_etchash);
        BOOST_CHECK_EQUAL(chain[i]->nAsertAnchorRandomXHeight, incremental[i].anchor_randomx);
        BOOST_CHECK_EQUAL(chain[i]->nAsertAnchorEtchashHeight, incremental[i].anchor_etchash);
    }

    // The rebuilt state still describes the re-anchor, not the activation
    // anchor: Etchash resumed at 160, RandomX last seen at 164.
    BOOST_CHECK_EQUAL(chain.back()->nLastEtchashHeight, 160);
    BOOST_CHECK_EQUAL(chain.back()->nAsertAnchorEtchashHeight, 160);
    BOOST_CHECK_EQUAL(chain.back()->nLastRandomXHeight, 164);
    BOOST_CHECK_EQUAL(chain.back()->nAsertAnchorRandomXHeight, 149);
}

// !RCPU (dual-algo): a chain whose schedule would make the difficulty code
// divide by zero or fall back to powLimit (no difficulty at all) must be
// rejected when its parameters are built, not at the first block that reaches
// the bad path. The assertion inside GetDualAlgoWorkRequired() is compiled out
// of release builds, so this check is what protects a third-party chain or a
// regtest from silently accepting minimum-difficulty blocks.
BOOST_AUTO_TEST_CASE(dual_algo_parameter_validation)
{
    // A chain that does not opt in is always valid.
    BOOST_CHECK_NO_THROW(Consensus::Params{}.CheckDualAlgoConsistency());

    // Mainnet-like schedule.
    Consensus::Params params;
    // These two have no default initializer in Consensus::Params (upstream
    // Bitcoin sets them in chainparams), so the test must set them explicitly.
    params.fPowNoRetargeting = false;
    params.fPowAllowMinDifficultyBlocks = false;
    params.nDualAlgoActivationHeight = 21000;
    params.nDualAlgoTargetSpacing = 600;
    params.nASERTHalfLife = 2 * 24 * 60 * 60;
    params.nDualAlgoReanchorGap = 12 * 60 * 60;
    params.nDualAlgoMaxDrop = 8;
    params.nDualAlgoLatenessFixHeight = 29909;
    params.nDualAlgoNeverMinedFixHeight = std::numeric_limits<int>::max();
    BOOST_CHECK_NO_THROW(params.CheckDualAlgoConsistency());

    // Height 0 makes GetAncestor(nActHeight - 1) return nullptr, after which the
    // difficulty code returns powLimit (no difficulty). Negative heights are in
    // the same class of misconfiguration.
    Consensus::Params bad{params};
    bad.nDualAlgoActivationHeight = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);
    bad.nDualAlgoActivationHeight = -5;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);

    // Division-by-zero paths inside CalculateASERT().
    bad = params;
    bad.nDualAlgoTargetSpacing = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);
    bad = params;
    bad.nASERTHalfLife = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);

    // Etchash epoch length is used by GetEtchashEpoch() on every dual chain.
    bad = params;
    bad.nEtchashEpochLength = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);
    bad = params;
    bad.nEtchashEpochLength = -1;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);

    // A cap below 1 ("at most 0x") is never intended.
    bad = params;
    bad.nDualAlgoMaxDrop = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);

    // A zero re-anchor gap would re-anchor on every single block.
    bad = params;
    bad.nDualAlgoReanchorGap = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);

    // Negative activation heights are the misconfiguration; 0 means "from the
    // first dual-algo block" and INT_MAX means "never", both legitimate.
    bad = params;
    bad.nDualAlgoLatenessFixHeight = -1;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);
    bad = params;
    bad.nDualAlgoNeverMinedFixHeight = -1;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);
    bad = params;
    bad.nDualAlgoLatenessFixHeight = 0;
    bad.nDualAlgoNeverMinedFixHeight = params.nDualAlgoActivationHeight;
    BOOST_CHECK_NO_THROW(bad.CheckDualAlgoConsistency());

    // INT_MAX ("never") stays valid for both fix heights.
    bad = params;
    bad.nDualAlgoLatenessFixHeight = std::numeric_limits<int>::max();
    bad.nDualAlgoNeverMinedFixHeight = std::numeric_limits<int>::max();
    BOOST_CHECK_NO_THROW(bad.CheckDualAlgoConsistency());

    // Regtest (fPowNoRetargeting) returns the reference target before it reaches
    // ASERT or the re-anchor rule, so those parameters are unused there and must
    // not be required. The activation height is still needed for dispatch.
    bad = params;
    bad.fPowNoRetargeting = true;
    bad.nASERTHalfLife = 0;
    bad.nDualAlgoTargetSpacing = 0;
    bad.nDualAlgoReanchorGap = 0;
    bad.nDualAlgoMaxDrop = 0;
    bad.nDualAlgoLatenessFixHeight = 0;
    bad.nDualAlgoNeverMinedFixHeight = 0;
    BOOST_CHECK_NO_THROW(bad.CheckDualAlgoConsistency());
    bad.nDualAlgoActivationHeight = 0;
    BOOST_CHECK_THROW(bad.CheckDualAlgoConsistency(), std::runtime_error);
}

namespace {
/** Build a regtest-like chain for the POW-02 never-mined tests.
 *
 *  Heights 140..159 are created with the version at the same position in
 *  `versions`. The caller then moves the tip's timestamp to create the desired
 *  lateness.
 */
std::vector<std::unique_ptr<CBlockIndex>> MakeNeverMinedChain(const Consensus::Params& params,
                                                              const std::vector<int32_t>& versions)
{
    std::vector<std::unique_ptr<CBlockIndex>> chain;
    const int64_t nBaseTime{1710000000};
    int height = 140;
    for (int32_t version : versions) {
        auto index = std::make_unique<CBlockIndex>();
        index->nHeight = height;
        index->pprev = chain.empty() ? nullptr : chain.back().get();
        index->nTime = nBaseTime + (height - 140) * 300;
        index->nBits = 0x1f00ffff;
        index->nVersion = version;
        index->nChainWork = (index->pprev ? index->pprev->nChainWork : arith_uint256(0)) + GetBlockProof(*index);
        index->nSecondAlgoCount = ComputeSecondAlgoCount(*index, params.nDualAlgoActivationHeight);
        UpdateDualAlgoState(*index, params);
        chain.push_back(std::move(index));
        ++height;
    }
    return chain;
}

Consensus::Params NeverMinedParams()
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nDualAlgoTargetSpacing = 600;
    params.nPowTargetSpacing = 300;
    params.nASERTHalfLife = 2 * 24 * 60 * 60;
    params.nEtchashEpochLength = 10;
    params.fPowAllowMinDifficultyBlocks = false;
    params.fPowNoRetargeting = false;
    params.nDualAlgoLatenessFixHeight = 160;
    params.nDualAlgoReanchorGap = 12 * 60 * 60;
    params.nDualAlgoMaxDrop = 8;
    params.nDualAlgoNeverMinedFixHeight = 160;
    return params;
}
} // namespace

// !RCPU (POW-02): an algorithm that has produced no block since activation has
// no last-same block, so the H-1 re-anchor can never fire for it. Before the
// fix every block recomputed the full activation lateness and the target stayed
// pinned at powLimit forever. The first block must be capped by
// nDualAlgoMaxDrop AND become the new anchor, otherwise the next block falls
// back into the same trap.
BOOST_AUTO_TEST_CASE(dual_algo_never_mined_first_block_is_bounded)
{
    const Consensus::Params base_params{NeverMinedParams()};
    // 140..159 are all RandomX: Etchash has never mined.
    std::vector<int32_t> versions(20, 0);
    auto chain = MakeNeverMinedChain(base_params, versions);
    CBlockIndex* tip = chain.back().get();

    BOOST_CHECK_EQUAL(tip->nLastEtchashHeight, -1);
    BOOST_CHECK_EQUAL(tip->nAsertAnchorEtchashHeight, 149);

    // 60 days of lateness measured from the activation anchor. Two days of
    // half-life means this saturates at powLimit under the old rule.
    const int64_t nActivationAnchorTime{1710000000 + (149 - 140) * 300};
    tip->nTime = nActivationAnchorTime + 60 * 24 * 60 * 60;

    CBlockHeader header;
    header.nTime = tip->nTime + 600;
    header.nVersion = BLOCK_VERSION_QHASH;

    // H2 - 1: the old rule releases the banked lateness as powLimit.
    Consensus::Params params{base_params};
    params.nDualAlgoNeverMinedFixHeight = 161;
    const uint32_t old_bits{GetNextWorkRequired(tip, &header, params)};
    BOOST_CHECK_EQUAL(old_bits, UintToArith256(params.powLimit).GetCompact());

    // H2: the first block is capped and must not be powLimit.
    params.nDualAlgoNeverMinedFixHeight = 160;
    const uint32_t resume_bits{GetNextWorkRequired(tip, &header, params)};
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1f00ffff);
    refTarget >>= 1; // activation anchor is made twice as hard
    const arith_uint256 capped = refTarget * arith_uint256{params.nDualAlgoMaxDrop};
    BOOST_CHECK_EQUAL(resume_bits, capped.GetCompact());
    BOOST_CHECK(resume_bits != UintToArith256(params.powLimit).GetCompact());

    // The resume block becomes the Etchash anchor; the other algorithm is
    // untouched.
    auto resume = std::make_unique<CBlockIndex>();
    resume->nHeight = 160;
    resume->pprev = tip;
    resume->nTime = header.nTime;
    resume->nBits = resume_bits;
    resume->nVersion = BLOCK_VERSION_QHASH;
    resume->nChainWork = tip->nChainWork + GetBlockProof(*resume);
    resume->nSecondAlgoCount = ComputeSecondAlgoCount(*resume, params.nDualAlgoActivationHeight);
    UpdateDualAlgoState(*resume, params);
    BOOST_CHECK_EQUAL(resume->nLastEtchashHeight, 160);
    BOOST_CHECK_EQUAL(resume->nAsertAnchorEtchashHeight, 160);
    BOOST_CHECK_EQUAL(resume->nLastRandomXHeight, 159);
    BOOST_CHECK_EQUAL(resume->nAsertAnchorRandomXHeight, 149);

    // The next Etchash block must not fall back to the activation anchor.
    CBlockHeader next_header;
    next_header.nTime = resume->nTime + 600;
    next_header.nVersion = BLOCK_VERSION_QHASH;
    const uint32_t next_bits{GetNextWorkRequired(resume.get(), &next_header, params)};
    BOOST_CHECK(next_bits != UintToArith256(params.powLimit).GetCompact());
    BOOST_CHECK(next_bits <= resume_bits);
}

// Mirror case: Etchash has history and RandomX has never mined.
BOOST_AUTO_TEST_CASE(dual_algo_never_mined_randomx_mirror)
{
    const Consensus::Params params{NeverMinedParams()};
    std::vector<int32_t> versions(20, BLOCK_VERSION_QHASH);
    auto chain = MakeNeverMinedChain(params, versions);
    CBlockIndex* tip = chain.back().get();

    BOOST_CHECK_EQUAL(tip->nLastRandomXHeight, -1);
    BOOST_CHECK_EQUAL(tip->nAsertAnchorRandomXHeight, 149);

    const int64_t nActivationAnchorTime{1710000000 + (149 - 140) * 300};
    tip->nTime = nActivationAnchorTime + 60 * 24 * 60 * 60;

    CBlockHeader header;
    header.nTime = tip->nTime + 600;
    header.nVersion = 0;

    Consensus::Params no_fix{params};
    no_fix.nDualAlgoNeverMinedFixHeight = 161;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(tip, &header, no_fix),
                      UintToArith256(params.powLimit).GetCompact());

    const uint32_t resume_bits{GetNextWorkRequired(tip, &header, params)};
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1f00ffff);
    refTarget >>= 1;
    const arith_uint256 capped = refTarget * arith_uint256{params.nDualAlgoMaxDrop};
    BOOST_CHECK_EQUAL(resume_bits, capped.GetCompact());
    BOOST_CHECK(resume_bits != UintToArith256(params.powLimit).GetCompact());

    auto resume = std::make_unique<CBlockIndex>();
    resume->nHeight = 160;
    resume->pprev = tip;
    resume->nTime = header.nTime;
    resume->nBits = resume_bits;
    resume->nVersion = 0;
    resume->nChainWork = tip->nChainWork + GetBlockProof(*resume);
    resume->nSecondAlgoCount = ComputeSecondAlgoCount(*resume, params.nDualAlgoActivationHeight);
    UpdateDualAlgoState(*resume, params);
    BOOST_CHECK_EQUAL(resume->nLastRandomXHeight, 160);
    BOOST_CHECK_EQUAL(resume->nAsertAnchorRandomXHeight, 160);
    BOOST_CHECK_EQUAL(resume->nLastEtchashHeight, 159);
}

// Just above the re-anchor threshold the full gap must feed ASERT, and the
// result must be the uncapped ASERT value (the cap only binds from ~6 days).
// Truncating the gap to nDualAlgoReanchorGap would produce a different target,
// so this also guards against the min(gap, 12h) shortcut.
BOOST_AUTO_TEST_CASE(dual_algo_never_mined_above_threshold_uses_full_gap)
{
    const Consensus::Params params{NeverMinedParams()};
    std::vector<int32_t> versions(20, 0);
    auto chain = MakeNeverMinedChain(params, versions);
    CBlockIndex* tip = chain.back().get();

    const int64_t nActivationAnchorTime{1710000000 + (149 - 140) * 300};
    const int64_t nGap{13 * 60 * 60};
    tip->nTime = nActivationAnchorTime + nGap;

    CBlockHeader header;
    header.nTime = tip->nTime + 600;
    header.nVersion = BLOCK_VERSION_QHASH;

    arith_uint256 refTarget;
    refTarget.SetCompact(0x1f00ffff);
    refTarget >>= 1;
    const uint32_t expected{CalculateASERT(refTarget,
                                           params.nDualAlgoTargetSpacing,
                                           nGap,
                                           /*nHeightDiff=*/0,
                                           UintToArith256(params.powLimit),
                                           params.nASERTHalfLife)
                                .GetCompact()};

    const uint32_t bits{GetNextWorkRequired(tip, &header, params)};
    BOOST_CHECK_EQUAL(bits, expected);

    const arith_uint256 capped = refTarget * arith_uint256{params.nDualAlgoMaxDrop};
    BOOST_CHECK(bits != capped.GetCompact());
    BOOST_CHECK(bits != UintToArith256(params.powLimit).GetCompact());
}

// Chains without retargeting (and therefore without ASERT) must be untouched:
// the target stays at the activation-initialised value and no anchor moves.
BOOST_AUTO_TEST_CASE(dual_algo_never_mined_ignored_without_retargeting)
{
    Consensus::Params params{NeverMinedParams()};
    params.fPowNoRetargeting = true;
    std::vector<int32_t> versions(20, 0);
    auto chain = MakeNeverMinedChain(params, versions);
    CBlockIndex* tip = chain.back().get();

    const int64_t nActivationAnchorTime{1710000000 + (149 - 140) * 300};
    tip->nTime = nActivationAnchorTime + 60 * 24 * 60 * 60;

    CBlockHeader header;
    header.nTime = tip->nTime + 600;
    header.nVersion = BLOCK_VERSION_QHASH;

    arith_uint256 refTarget;
    refTarget.SetCompact(0x1f00ffff);
    refTarget >>= 1;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(tip, &header, params), refTarget.GetCompact());

    auto resume = std::make_unique<CBlockIndex>();
    resume->nHeight = 160;
    resume->pprev = tip;
    resume->nTime = header.nTime;
    resume->nBits = refTarget.GetCompact();
    resume->nVersion = BLOCK_VERSION_QHASH;
    resume->nChainWork = tip->nChainWork + GetBlockProof(*resume);
    resume->nSecondAlgoCount = ComputeSecondAlgoCount(*resume, params.nDualAlgoActivationHeight);
    UpdateDualAlgoState(*resume, params);
    BOOST_CHECK_EQUAL(resume->nAsertAnchorEtchashHeight, 149);
}

// !RCPU (dual-algo PoW dispatch): HasValidProofOfWork() must follow
// GetPowAlgoForHeader exactly:
//   - post-activation + bit-30 + height-known -> Etchash only
//   - pre-activation  + bit-30 + height-known -> RandomX (bit ignored)
//   - height unknown  + bit-30                -> defer (no verdict)
BOOST_AUTO_TEST_CASE(etchash_chain_pow_dispatch_strict)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;

    static auto make_hdr = [](int32_t version) {
        CBlockHeader h;
        h.nVersion = version;
        h.hashPrevBlock  = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
        h.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
        h.nTime   = 1710000000;
        h.nBits   = 0x207fffff;
        h.nNonce  = 0;
        h.hashRandomX.SetNull();
        return h;
    };

    // Mine an Etchash result. Only valid at/after the activation height.
    static auto mine_etchash = [](CBlockHeader& h, int64_t height, const Consensus::Params& p) {
        uint256 solved;
        for (uint32_t nonce = 0; nonce < 100000; ++nonce) {
            h.nNonce = nonce;
            if (CheckProofOfWorkEtchash(h, height, p, POW_VERIFY_MINING, &solved)) {
                h.hashRandomX = solved;
                return true;
            }
        }
        return false;
    };
    static auto mine_randomx = [](CBlockHeader& h, const Consensus::Params& p) {
        uint256 solved;
        for (uint32_t nonce = 0; nonce < 100000; ++nonce) {
            h.nNonce = nonce;
            if (CheckProofOfWorkRandomX(h, p, POW_VERIFY_MINING, &solved, /*prevBlockTime=*/0)) {
                h.hashRandomX = solved;
                return true;
            }
        }
        return false;
    };

    // Case 1: post-activation + bit-30 + Etchash proof + height-known -> OK
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_etchash(h, 150, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/150));
    }

    // Case 2: post-activation + bit-30 + RandomX proof + height-known -> FAIL
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_randomx(h, params));
        BOOST_CHECK(!HasValidProofOfWork({h}, params, /*nStartHeight=*/150));
    }

    // Case 3 (merged former pre-activation qhash case): pre-activation +
    // bit-30 + height-known -> dispatch returns RandomX. Etchash cannot be
    // mined below activation, so only a RandomX proof is meaningful here.
    {
        BOOST_CHECK(GetPowAlgoForHeader(149, 1 | BLOCK_VERSION_QHASH, params) == std::optional<PowAlgo>(PowAlgo::RandomX));
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_randomx(h, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/149));
    }

    // Case 4: height-unknown + bit-30 -> defer without touching PoW.
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        *h.hashRandomX.begin() = 0x01; // deliberately not a valid proof
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/-1));
    }

    // Case 5: post-activation + tampered Etchash proof + height-known -> FAIL
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_etchash(h, 150, params));
        CBlockHeader tampered = h;
        *tampered.hashRandomX.begin() ^= 0x01;
        BOOST_CHECK(!HasValidProofOfWork({tampered}, params, /*nStartHeight=*/150));
    }

    // Case 6: bit-30 clear + RandomX proof + post-activation height -> OK
    {
        CBlockHeader h = make_hdr(1);
        BOOST_REQUIRE(mine_randomx(h, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/150));
    }
}

// Mainnet-parameter KAT for the 21000 activation boundary. This pins the
// height/epoch/BE/bit-30 dispatch that the deployed mainnet params use, even
// though the unit fixture builds Consensus::Params by hand (CMainParams is not
// cheap to instantiate inside a unit test).
BOOST_AUTO_TEST_CASE(etchash_mainnet_21000_boundaries)
{
    Consensus::Params params;

    params.nDualAlgoActivationHeight = 21000;
    params.nEtchashEpochLength = 6000;
    params.nEtchashBigEndianHeight = 21000;

    BOOST_CHECK_EQUAL(GetEtchashEpoch(20999, params), -1);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(21000, params), 0);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(26999, params), 0);
    BOOST_CHECK_EQUAL(GetEtchashEpoch(27000, params), 1);
    BOOST_CHECK(GetEtchashSeedHash(21000, params).IsNull());
    BOOST_CHECK(GetEtchashSeedHash(27000, params) == etchash::SeedForEpoch(1));

    BOOST_CHECK(GetPowAlgoForHeader(20999, BLOCK_VERSION_QHASH, params) == PowAlgo::RandomX);
    BOOST_CHECK(GetPowAlgoForHeader(21000, BLOCK_VERSION_QHASH, params) == PowAlgo::Etchash);
    BOOST_CHECK(GetPowAlgoForHeader(21000, 0x20000000, params) == PowAlgo::RandomX);
}

BOOST_AUTO_TEST_CASE(dual_algo_never_mined_is_scheduled_on_live_chains)
{
    // POW-02 only runs where retargeting runs, so the live chains must carry an
    // explicit height. Regtest keeps fPowNoRetargeting and its 0 is intent only.
    // If this fails after an intentional re-schedule, update the expected height
    // together with chainparams and the release notes.
    const std::array<std::pair<ChainType, int>, 2> live{{
        {ChainType::RCPUMAIN, 31939},
        {ChainType::RCPUTESTNET, 31939},
    }};
    for (const auto& [chain, expected] : live) {
        const auto chain_params = CreateChainParams(*m_node.args, chain);
        const auto& params = chain_params->GetConsensus();
        BOOST_CHECK_EQUAL(params.nDualAlgoActivationHeight, 21000);
        BOOST_CHECK_EQUAL(params.nDualAlgoLatenessFixHeight, 29909);
        BOOST_CHECK_EQUAL(params.nDualAlgoNeverMinedFixHeight, expected);
        BOOST_CHECK(params.nDualAlgoNeverMinedFixHeight > params.nDualAlgoLatenessFixHeight);
        BOOST_CHECK_NO_THROW(params.CheckDualAlgoConsistency());
    }

    const auto regtest = CreateChainParams(*m_node.args, ChainType::RCPUREGTEST);
    BOOST_CHECK_EQUAL(regtest->GetConsensus().nDualAlgoNeverMinedFixHeight, 0);
}

BOOST_AUTO_TEST_SUITE_END()
