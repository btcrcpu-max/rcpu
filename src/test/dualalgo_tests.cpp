// Copyright (c) 2024-2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <crypto/etchash.h>
#include <crypto/keccak.h>
#include <crypto/qhash.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <span.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <validation.h>

#include <memory>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(dualalgo_tests, BasicTestingSetup)

// !RCPU (dual-algo): qhash must be a pure deterministic function of the seed.
BOOST_AUTO_TEST_CASE(qhash_deterministic)
{
    uint256 seedA = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    uint256 seedB = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");

    uint256 hA1 = QHashFromSeed(seedA.begin());
    uint256 hA2 = QHashFromSeed(seedA.begin());
    uint256 hB = QHashFromSeed(seedB.begin());

    BOOST_CHECK(hA1 == hA2);
    BOOST_CHECK(hA1 != hB);
    // Avalanche sanity: a 1-bit seed change must not leave the output unchanged.
    BOOST_CHECK(hA1 != seedA && hB != seedB);
}

// !RCPU (dual-algo): end-to-end qhash proof-of-work semantics — a solved header
// verifies, a tampered commitment field fails, and the genesis short-circuit
// still applies.
BOOST_AUTO_TEST_CASE(qhash_pow_roundtrip)
{
    // Loose regtest-style target: 0x207fffff accepts ~50% of hashes.
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;

    CBlockHeader block;
    block.nVersion = 1 | BLOCK_VERSION_QHASH;
    block.hashPrevBlock = uint256S("0x0000000000000000000000000000000000000000000000000000000000000001");
    block.hashMerkleRoot = uint256S("0x0000000000000000000000000000000000000000000000000000000000000002");
    block.nTime = 1710000000;
    block.nBits = 0x207fffff;
    block.nNonce = 0;

    // Solve by incrementing the nonce (each nonce yields a fresh seed).
    uint256 solved;
    bool found = false;
    for (uint32_t nonce = 0; nonce < 1000; ++nonce) {
        block.nNonce = nonce;
        if (CheckProofOfWorkQHash(block, params, POW_VERIFY_MINING, &solved)) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
    block.hashRandomX = solved;

    // Full verification accepts the solved header.
    BOOST_CHECK(CheckProofOfWorkQHash(block, params, POW_VERIFY_FULL));

    // Tampering with the commitment field must fail.
    CBlockHeader tampered = block;
    *tampered.hashRandomX.begin() ^= 0x01;
    BOOST_CHECK(!CheckProofOfWorkQHash(tampered, params, POW_VERIFY_FULL));

    // Genesis-style headers (null prev) short-circuit regardless of content.
    CBlockHeader genesis_like = block;
    genesis_like.hashPrevBlock.SetNull();
    genesis_like.hashRandomX.SetNull();
    uint256 out;
    BOOST_CHECK(CheckProofOfWorkQHash(genesis_like, params, POW_VERIFY_FULL, &out));
    BOOST_CHECK(out.IsNull());
}

// !RCPU (dual-algo): the qhash marker lives in nVersion bit 30, must not be
// confused with the versionbits deployment bits (0..28) and must survive the
// header copy used by miners.
BOOST_AUTO_TEST_CASE(qhash_version_flag)
{
    static_assert(BLOCK_VERSION_QHASH == 0x40000000, "qhash marker is nVersion bit 30");
    BOOST_CHECK_EQUAL(BLOCK_VERSION_QHASH & 0x1fffffff, 0);

    CBlock block;
    block.SetNull();
    block.nVersion = 0x20000000; // versionbits top bits, no qhash
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
// caches with header_hash = keccak256("rcpu etchash vector"), nonce 42 —
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

// !RCPU (dual-algo etchash): end-to-end etchash proof-of-work semantics —
// epoch plumbing, solve/verify roundtrip, tamper rejection, pre-activation
// refusal, and epoch-switch rejection.
BOOST_AUTO_TEST_CASE(etchash_pow_roundtrip)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;
    params.fDualAlgoEtchash = true;

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

// !RCPU (dual-algo etchash): regression for the network-layer headers pre-check.
// HasValidProofOfWork() used to probe only qhash + RandomX for bit-30 headers,
// so every relayed Etchash header looked like invalid PoW and cost the peer 100
// misbehaviour points (disconnect). It must verify Etchash when the height is
// known and defer (not reject) when the height is unknown.
BOOST_AUTO_TEST_CASE(etchash_header_pow_precheck_is_height_aware)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.nEtchashEpochLength = 10;
    params.fDualAlgoEtchash = true;

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

    // Pre-activation fix this returned false: qhash does not match an Etchash
    // result and the RandomX commitment is unrelated.
    BOOST_CHECK(HasValidProofOfWork({header}, params, /*nStartHeight=*/150));

    // Height unknown: deferred to AcceptBlockHeader, must not be rejected here.
    BOOST_CHECK(HasValidProofOfWork({header}, params, /*nStartHeight=*/-1));

    // Tampering with the stored Etchash result must still be caught.
    CBlockHeader tampered = header;
    *tampered.hashRandomX.begin() ^= 0x01;
    BOOST_CHECK(!HasValidProofOfWork({tampered}, params, /*nStartHeight=*/150));
}

// !RCPU (dual-algo etchash): a valid Etchash block whose parent is unknown must
// not be rejected as "high-hash" by the context-free CheckBlock(). Before the
// fix the qhash/RandomX probe rejected it, which marked the relaying peer as
// misbehaving (BLOCK_INVALID_HEADER) for ordinary out-of-order relay.
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
    params.fDualAlgoEtchash = true;
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
    params.fDualAlgoEtchash = true;
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

// !RCPU (dual-algo PoW dispatch): qhash 链 (fDualAlgoEtchash=false) 的
// HasValidProofOfWork() 行为边界。helper 统一 dispatch 后，该函数在
// 高度已知场景不再宽松地同时接受 qhash 和 RandomX 证明，而是严格跟随
// GetPowAlgoForHeader 的判定：
//   - post-activation + bit-30 + height-known  → 只认 QHash（qhash 承诺会重算）
//   - pre-activation  + bit-30 + height-known  → 判 RandomX（共识忽略 bit-30）；
//     预检层 RandomX 只做承诺目标检查（数据集太贵不重算），权威拒绝在
//     CheckBlockHeader 的 POW_VERIFY_FULL
//   - height unknown + bit-30                 → defer（不 reject）
// 这组测试锁住上述收紧行为，防止未来有人把宽容 pre-check 加回来。
BOOST_AUTO_TEST_CASE(qhash_chain_pow_precheck_uses_helper)
{
    Consensus::Params params;
    params.powLimit = uint256S("0x7fffffff00000000000000000000000000000000000000000000000000000000");
    params.nDualAlgoActivationHeight = 150;
    params.fDualAlgoEtchash = false; // legacy qhash chain

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

    // 两种挖矿辅助：分别产出 qhash 证明和 RandomX 证明并存入 hashRandomX。
    static auto mine_qhash = [](CBlockHeader& h, const Consensus::Params& p) {
        uint256 solved;
        for (uint32_t nonce = 0; nonce < 100000; ++nonce) {
            h.nNonce = nonce;
            if (CheckProofOfWorkQHash(h, p, POW_VERIFY_MINING, &solved)) {
                h.hashRandomX = solved;
                return true;
            }
        }
        return false;
    };
    static auto mine_randomx = [](CBlockHeader& h, const Consensus::Params& p) {
        uint256 solved;
        // prevBlockTime=0 让 RandomX 按 block.nTime 计算 epoch；
        // 测试 powLimit 宽松，100000 次尝试内必出。
        for (uint32_t nonce = 0; nonce < 100000; ++nonce) {
            h.nNonce = nonce;
            if (CheckProofOfWorkRandomX(h, p, POW_VERIFY_MINING, &solved, /*prevBlockTime=*/0)) {
                h.hashRandomX = solved;
                return true;
            }
        }
        return false;
    };

    // ── Case 1: post-activation + bit-30 + qhash proof + height-known → OK ──
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_qhash(h, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/150));
    }

    // ── Case 2: post-activation + bit-30 + RandomX proof + height-known → FAIL ──
    // helper 返回 PowAlgo::QHash，RandomX proof 被拒。改前宽容双路都能过。
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_randomx(h, params));
        BOOST_CHECK(!HasValidProofOfWork({h}, params, /*nStartHeight=*/150));
    }

    // ── Case 3: pre-activation + bit-30 + height-known → helper 判 RandomX ──
    // 直接锁定 dispatch 判定。预检层（COMMITMENT_ONLY）的 RandomX 只做承诺
    // 目标检查、不重算数据集，因此满足目标的 qhash 承诺在预检层被接受；
    // 真正的拒绝发生在 CheckBlockHeader 的 POW_VERIFY_FULL RandomX 重算。
    {
        BOOST_CHECK(GetPowAlgoForHeader(149, 1 | BLOCK_VERSION_QHASH, params) == std::optional<PowAlgo>(PowAlgo::RandomX));
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_qhash(h, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/149));
    }

    // ── Case 4: pre-activation + bit-30 + RandomX proof + height-known → OK ──
    // helper 正确忽略 pre-activation 的 bit-30，返回 RandomX。
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_randomx(h, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/149));
    }

    // ── Case 5: height-unknown + bit-30 + tampered proof → defer ──
    // helper 返回 nullopt，HasValidProofOfWork 跳过判定；
    // 即使 proof 被改过，这里也不应该 reject（defer 给 AcceptBlockHeader）。
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_qhash(h, params));
        CBlockHeader tampered = h;
        *tampered.hashRandomX.begin() ^= 0x01;
        BOOST_CHECK(HasValidProofOfWork({tampered}, params, /*nStartHeight=*/-1));
    }

    // ── Case 6: post-activation + tampered qhash + height-known → FAIL ──
    // 有高度 + tampered proof 必须被拒。
    {
        CBlockHeader h = make_hdr(1 | BLOCK_VERSION_QHASH);
        BOOST_REQUIRE(mine_qhash(h, params));
        CBlockHeader tampered = h;
        *tampered.hashRandomX.begin() ^= 0x01;
        BOOST_CHECK(!HasValidProofOfWork({tampered}, params, /*nStartHeight=*/150));
    }

    // ── Case 7: bit-30 clear + RandomX proof + post-activation height → OK ──
    // bit-30 清零时 helper 永远返回 RandomX，不受激活高度影响。
    {
        CBlockHeader h = make_hdr(1);
        BOOST_REQUIRE(mine_randomx(h, params));
        BOOST_CHECK(HasValidProofOfWork({h}, params, /*nStartHeight=*/150));
    }
}

BOOST_AUTO_TEST_SUITE_END()
