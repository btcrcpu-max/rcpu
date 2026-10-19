// Copyright (c) 2024-2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <consensus/params.h>
#include <crypto/qhash.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <uint256.h>

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

BOOST_AUTO_TEST_SUITE_END()
