// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <blind.h>
#include <key.h>
#include <primitives/confidential.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(blind_tests, BasicTestingSetup)

// Blind an output, then recover the committed amount and blinding factor
// using the stored nonce / range-proof rewind. Exercises range-proof creation,
// Pedersen commitment construction and the corresponding consensus unblind path.
BOOST_AUTO_TEST_CASE(roundtrip_unblind)
{
    const CAmount amount = 1234567;

    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;
    uint256 nonce;

    BOOST_REQUIRE(BlindOutput(conf_value, nonce_commit, rangeproof, blind, nonce, amount));
    BOOST_CHECK(!conf_value.IsExplicit());
    BOOST_CHECK(conf_value.IsCommitment());
    BOOST_CHECK(!rangeproof.empty());

    CAmount amount_out = -1;
    uint256 blind_out;
    BOOST_REQUIRE(UnblindValue(conf_value, nonce_commit, rangeproof, amount_out, blind_out));
    BOOST_CHECK_EQUAL(amount_out, amount);
    BOOST_CHECK(blind_out == blind);
}

// Blind to a specific recipient and recover using only the recipient key via ECDH.
BOOST_AUTO_TEST_CASE(blind_to_recipient_roundtrip)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    BOOST_REQUIRE(recv_key.IsValid());
    const CPubKey recv_pub = recv_key.GetPubKey();

    const CAmount amount = 21001;
    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;

    BOOST_REQUIRE(BlindOutputToRecipient(conf_value, nonce_commit, rangeproof, blind, amount, recv_pub));
    BOOST_CHECK(conf_value.IsCommitment());
    BOOST_CHECK(!rangeproof.empty());

    CAmount amount_out = -1;
    uint256 blind_out;
    BOOST_REQUIRE(UnblindValueWithKey(recv_key, conf_value, nonce_commit, rangeproof, amount_out, blind_out));
    BOOST_CHECK_EQUAL(amount_out, amount);
    BOOST_CHECK(blind_out == blind);
}

BOOST_AUTO_TEST_SUITE_END()