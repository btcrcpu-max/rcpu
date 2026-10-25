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
        const uint64_t size = etchash::CalcCacheSize(0);
        const std::vector<uint32_t> cache = etchash::GenerateCache(size, etchash::SeedForEpoch(0));
        etchash::HashimotoLight(size, cache, header_hash, nonce, digest, result);
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{digest, 32}),
            "f8bc0eaf90805c3635020904e3f246d1d52b2eebc6e7f4d2e9b5e4a95d72e3df");
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{result, 32}),
            "5bea955a8035c5b5226a447677b25927e8428bc578aa4ae5a06abb1e60123a39");
    }

    {
        const uint64_t size = etchash::CalcCacheSize(1);
        const std::vector<uint32_t> cache = etchash::GenerateCache(size, etchash::SeedForEpoch(1));
        etchash::HashimotoLight(size, cache, header_hash, nonce, digest, result);
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{digest, 32}),
            "4ac9c855055b36a204828196f892fbb591f2509ed025871b6f1c1418d28a4a92");
        BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>{result, 32}),
            "aee2525dc2948c931c7e94b7385c822c55abd7648f89e2bcd09591fdc0372a4f");
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

BOOST_AUTO_TEST_SUITE_END()
