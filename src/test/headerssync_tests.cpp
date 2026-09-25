// Copyright (c) 2022 The Bitcoin Core developers
// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// !RCPU
// Regression tests for the v1.0.25 Windows IBD "high-hash" bug (commit
// f7ac48c): CompressedHeader previously dropped the 32-byte hashRandomX
// commitment field, so GetFullHeader() reconstructed a header that could not
// reproduce the on-chain block hash, and headers presync aborted around
// height ~4000 with "high-hash, proof of work failed". These tests pin the
// compress/restore contract so a future headersync layout change cannot
// silently regress again.
// !RCPU END

#include <headerssync.h>
#include <primitives/block.h>
#include <uint256.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

namespace {

/** Temporarily flip the chain-level hashRandomX header-field flag (A-02).
 *  CBlockHeader::GetHash() and SERIALIZE_METHODS branch on it; without the
 *  flag the 112-byte RandomX hash domain is not exercised. */
struct RandomXHeaderFieldGuard
{
    const bool m_saved;
    explicit RandomXHeaderFieldGuard(bool on) : m_saved(g_isRandomX.load()) { g_isRandomX.store(on); }
    ~RandomXHeaderFieldGuard() { g_isRandomX.store(m_saved); }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(headersync_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(compressed_header_roundtrip_preserves_hash_randomx)
{
    RandomXHeaderFieldGuard rx_guard(true);

    // A realistic RCPU header: the commitment is the mainnet genesis
    // hashRandomX (see src/kernel/chainparams.cpp), so GetHash() is computed
    // over the full 112-byte RandomX header domain.
    CBlockHeader orig;
    orig.nVersion = 1;
    orig.hashPrevBlock = uint256S("0x0");
    orig.hashMerkleRoot = uint256S("0x4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    orig.nTime = 1231006505;
    orig.nBits = 0x1d00ffff;
    orig.nNonce = 2083236893;
    orig.hashRandomX = uint256S("0x33c450e0152826e3a8948b01464cf9182344a1544b3ddcf6153dd04b62938d01");

    const uint256 prev_hash = uint256S("0x1111111111111111111111111111111111111111111111111111111111111111");

    // Compress and restore: every field, including hashRandomX, must survive
    // the round trip.
    const CompressedHeader compressed(orig);
    const CBlockHeader restored = compressed.GetFullHeader(prev_hash);

    BOOST_CHECK_EQUAL(restored.nVersion, orig.nVersion);
    BOOST_CHECK(restored.hashPrevBlock == prev_hash);
    BOOST_CHECK(restored.hashMerkleRoot == orig.hashMerkleRoot);
    BOOST_CHECK_EQUAL(restored.nTime, orig.nTime);
    BOOST_CHECK_EQUAL(restored.nBits, orig.nBits);
    BOOST_CHECK_EQUAL(restored.nNonce, orig.nNonce);
    BOOST_CHECK(restored.hashRandomX == orig.hashRandomX);

    // The reconstructed header must hash to the original block hash. Dropping
    // hashRandomX (pre-f7ac48c behaviour) makes this comparison fail, which is
    // exactly the ~4000-height "high-hash, proof of work failed" IBD failure.
    BOOST_CHECK(restored.GetHash() == orig.GetHash());

    // The commitment field is load-bearing in the hash domain: a header with a
    // nulled hashRandomX (as the buggy compression produced) does NOT hash to
    // the same block hash, even though all other fields are identical.
    CBlockHeader nulled = orig;
    nulled.hashRandomX.SetNull();
    BOOST_CHECK(nulled.GetHash() != orig.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()