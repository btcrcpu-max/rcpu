// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <blind.h>
#include <coins.h>
#include <key.h>
#include <primitives/confidential.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
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

// Regression test for the VerifyDB (level 3) "coin database inconsistencies
// found" failure on CT chains. Coin serialization used to route through
// TxOutCompression, which only persisted nValue and scriptPubKey and dropped
// nNonce and vchRangeproof. After restart, coins loaded from the chainstate
// DB had an empty nNonce, so CTxOut::operator== failed during
// DisconnectBlock's VerifyDB reconnect, producing the corrupted-database
// dialog. This test asserts that a Coin carrying a fully confidential output
// round-trips all CT fields intact.
BOOST_AUTO_TEST_CASE(coin_roundtrip_preserves_ct_fields)
{
    const CAmount amount = 424242;
    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;
    uint256 nonce;

    BOOST_REQUIRE(BlindOutput(conf_value, nonce_commit, rangeproof, blind, nonce, amount));
    BOOST_CHECK(conf_value.IsCommitment());
    BOOST_CHECK(!nonce_commit.IsNull());
    BOOST_CHECK(!rangeproof.empty());

    // A spendable CT output: committed value, recipient nonce, range proof
    // and a minimal scriptPubKey.
    CScript script_pubkey = CScript() << OP_TRUE;
    CTxOut out;
    out.nValue = conf_value;
    out.nNonce = nonce_commit;
    out.vchRangeproof = rangeproof;
    out.scriptPubKey = script_pubkey;

    const int nHeight = 1227;
    const bool fCoinBase = false;
    const Coin coin_in(out, nHeight, fCoinBase);

    // Serialize the coin exactly as it would be flushed to the chainstate DB.
    DataStream ss;
    ss << coin_in;

    // Deserialize it back, as happens when the coin is loaded after restart.
    Coin coin_out;
    ss >> coin_out;

    // The full CT prefix must survive the disk round-trip. Before the fix
    // nNonce was dropped (TxOutCompression omitted it) and this comparison
    // failed with "coin database inconsistencies found (last N blocks)".
    BOOST_CHECK(coin_out.out.nValue == coin_in.out.nValue);
    BOOST_CHECK(coin_out.out.nNonce == coin_in.out.nNonce);
    BOOST_CHECK(coin_out.out.vchRangeproof == coin_in.out.vchRangeproof);
    BOOST_CHECK(coin_out.out.scriptPubKey == coin_in.out.scriptPubKey);

    // CTxOut::operator== compares nValue, nNonce and scriptPubKey; it must
    // hold on the round-tripped output.
    BOOST_CHECK(coin_out.out == coin_in.out);

    // Metadata survives too.
    BOOST_CHECK(coin_out.nHeight == static_cast<uint32_t>(nHeight));
    BOOST_CHECK(coin_out.fCoinBase == fCoinBase);
    BOOST_CHECK(!coin_out.IsSpent());
}

BOOST_AUTO_TEST_SUITE_END()