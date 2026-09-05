// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <consensus/params.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <uint256.h>

BOOST_FIXTURE_TEST_SUITE(randomx_tests, BasicTestingSetup)

// Epoch must be a deterministic, monotonically non-decreasing function of
// time. This holds for any floor(t/duration) epoch definition.
BOOST_AUTO_TEST_CASE(epoch_monotonic)
{
    const uint32_t dur = 7 * 24 * 60 * 60; // one week
    const uint32_t t0 = 1700000000;

    BOOST_CHECK(GetEpoch(t0, dur) == GetEpoch(t0, dur));
    BOOST_CHECK(GetEpoch(t0, dur) <= GetEpoch(t0 + 1, dur));
    BOOST_CHECK(GetEpoch(t0, dur) <= GetEpoch(t0 + dur, dur));
    BOOST_CHECK(GetEpoch(t0, dur) <= GetEpoch(t0 + 2 * dur, dur));
}

// The seed hash is a deterministic function of the epoch.
BOOST_AUTO_TEST_CASE(seed_hash_deterministic)
{
    const uint32_t epoch = 42;
    const uint256 a = GetSeedHash(epoch);
    const uint256 b = GetSeedHash(epoch);
    const uint256 c = GetSeedHash(epoch + 1);

    BOOST_CHECK(a == b);
    BOOST_CHECK(!a.IsNull());
    BOOST_CHECK(a != c);
}

// A block whose target exceeds the network pow limit must always be rejected.
BOOST_AUTO_TEST_CASE(rejects_oversized_target)
{
    Consensus::Params params;
    params.fPowRandomX = true;
    params.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    CBlockHeader block;
    block.hashPrevBlock = uint256::ONE; // not the genesis block

    // nBits = 0xffffffff encodes a target far above the pow limit.
    block.nBits = 0xffffffff;
    BOOST_CHECK(!CheckProofOfWorkRandomX(block, params, POW_VERIFY_COMMITMENT_ONLY));
}

// In commitment-only verification, a block with an empty RandomX commitment
// must be rejected even if its target is otherwise within range.
BOOST_AUTO_TEST_CASE(rejects_null_commitment)
{
    Consensus::Params params;
    params.fPowRandomX = true;
    params.powLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    CBlockHeader block;
    block.hashPrevBlock = uint256::ONE;
    block.nBits = 0x1e3ffffc; // within pow limit

    BOOST_CHECK(!CheckProofOfWorkRandomX(block, params, POW_VERIFY_COMMITMENT_ONLY));
}

BOOST_AUTO_TEST_SUITE_END()