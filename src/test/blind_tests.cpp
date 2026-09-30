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
#include <secp256k1.h>
#include <secp256k1_generator.h>
#include <secp256k1_rangeproof.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <algorithm>
#include <optional>
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

// BlindOutputToRecipient must always emit an odd-Y compressed ephemeral
// pubkey (0x03 prefix) in the nonce commitment. A 0x02 prefix would be shaped
// exactly like a legacy path-A plaintext nonce, which IsLegacyNonceCommit
// misdetects (garbage rewind on unblind) and consensus rejects from
// nBanPathAHeight (bad-ct-legacy-nonce); ~50% of API outputs would fail
// otherwise. Loop several times since the odd/even Y split is 50/50.
BOOST_AUTO_TEST_CASE(blind_to_recipient_forces_odd_y_prefix)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    BOOST_REQUIRE(recv_key.IsValid());
    const CPubKey recv_pub = recv_key.GetPubKey();

    const CAmount amount = 1 * COIN;
    for (int i = 0; i < 20; ++i) {
        CConfidentialValue conf_value;
        CConfidentialNonce nonce_commit;
        std::vector<unsigned char> rangeproof;
        uint256 blind;

        BOOST_REQUIRE(BlindOutputToRecipient(conf_value, nonce_commit, rangeproof, blind, amount, recv_pub));
        BOOST_REQUIRE_EQUAL(nonce_commit.vchCommitment.size(), 33);
        BOOST_CHECK_EQUAL(nonce_commit.vchCommitment[0], 0x03);

        // Recipient-side ECDH unblind must still recover the amount.
        CAmount amount_out = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValueWithKey(recv_key, conf_value, nonce_commit, rangeproof, amount_out, blind_out));
        BOOST_CHECK_EQUAL(amount_out, amount);
        BOOST_CHECK(blind_out == blind);
    }
}

// GetNonce / UnblindValue must reject malformed nonce commitments instead of
// silently unwinding garbage. Path A (plaintext nonce) uses the 0x02 prefix;
// anything else -- wrong length or wrong prefix -- must fail closed.
BOOST_AUTO_TEST_CASE(unblind_rejects_bad_nonce_prefix)
{
    const CAmount amount = 123 * COIN;

    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;
    uint256 nonce;

    BOOST_REQUIRE(BlindOutput(conf_value, nonce_commit, rangeproof, blind, nonce, amount));
    BOOST_REQUIRE_EQUAL(nonce_commit.vchCommitment[0], 0x02);

    CAmount amt = -1;
    uint256 b;
    // Well-formed commitment from BlindOutput: rewinds fine.
    BOOST_REQUIRE(UnblindValue(conf_value, nonce_commit, rangeproof, amt, b));

    // Wrong prefix for path A: must fail, not parse the bytes as a nonce.
    CConfidentialNonce bad_prefix = nonce_commit;
    bad_prefix.vchCommitment[0] = 0x04;
    BOOST_CHECK(!UnblindValue(conf_value, bad_prefix, rangeproof, amt, b));

    // Wrong length: must fail before any memcpy.
    CConfidentialNonce short_nonce;
    short_nonce.vchCommitment.assign(nonce_commit.vchCommitment.begin(), nonce_commit.vchCommitment.begin() + 32);
    BOOST_CHECK(!UnblindValue(conf_value, short_nonce, rangeproof, amt, b));

    // GetNonce stays closed on the malformed variants.
    BOOST_CHECK(GetNonce(nonce_commit) == nonce);
    BOOST_CHECK(GetNonce(bad_prefix).IsNull());
    BOOST_CHECK(GetNonce(short_nonce).IsNull());
}

// GetOutputAmount must not conflate "unblind failed" with "amount is 0".
// Regression for the old signature that returned 0 on failure.
BOOST_AUTO_TEST_CASE(get_output_amount_optional_semantics)
{
    // Explicit output: exact amount.
    CTxOut explicit_out(777, CScript() << OP_TRUE);
    auto explicit_amt = GetOutputAmount(explicit_out);
    BOOST_REQUIRE(explicit_amt.has_value());
    BOOST_CHECK_EQUAL(*explicit_amt, 777);

    // Confidential output with a well-formed nonce: rewinds.
    const CAmount amount = 555;
    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;
    uint256 nonce;
    BOOST_REQUIRE(BlindOutput(conf_value, nonce_commit, rangeproof, blind, nonce, amount));

    CTxOut ct_out;
    ct_out.nValue = conf_value;
    ct_out.nNonce = nonce_commit;
    ct_out.vchRangeproof = rangeproof;
    ct_out.scriptPubKey = CScript() << OP_TRUE;

    auto ct_amt = GetOutputAmount(ct_out);
    BOOST_REQUIRE(ct_amt.has_value());
    BOOST_CHECK_EQUAL(*ct_amt, amount);

    // Corrupted prefix: must be nullopt, not 0 (callers then treat it as
    // "cannot determine", e.g. .value_or(0) at the specific call site).
    CTxOut bad_ct_out = ct_out;
    bad_ct_out.nNonce.vchCommitment[0] = 0x04;
    BOOST_CHECK(!GetOutputAmount(bad_ct_out).has_value());
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

static CTxOut MakeExplicitOut(CAmount amount)
{
    return CTxOut(amount, CScript() << OP_TRUE);
}

static CTxOut MakeFeeOut(CAmount fee)
{
    // explicit value + empty scriptPubKey
    return CTxOut(fee, CScript());
}

// All outputs confidential: last blind balances the rest; amounts rewind.
BOOST_AUTO_TEST_CASE(blind_tx_all_confidential)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(100000));
    tx.vout.push_back(MakeExplicitOut(200000));
    tx.vout.push_back(MakeExplicitOut(300000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds;
    std::vector<uint256> out_nonces;
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces));
    BOOST_REQUIRE_EQUAL(out_blinds.size(), 3U);

    for (size_t i = 0; i < tx.vout.size(); ++i) {
        BOOST_CHECK(!tx.vout[i].IsFee());
        BOOST_CHECK(tx.vout[i].nValue.IsCommitment());
        CAmount recovered = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValue(tx.vout[i].nValue, tx.vout[i].nNonce,
                                   tx.vout[i].vchRangeproof, recovered, blind_out));
        BOOST_CHECK_EQUAL(recovered, (i == 0 ? 100000 : i == 1 ? 200000 : 300000));
        BOOST_CHECK(blind_out == out_blinds[i]);
    }
}

// Fee in the middle must stay explicit; CT siblings still unblind.
BOOST_AUTO_TEST_CASE(blind_tx_middle_output_is_fee)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(500000));
    tx.vout.push_back(MakeFeeOut(10000));
    tx.vout.push_back(MakeExplicitOut(250000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds;
    std::vector<uint256> out_nonces;
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces));

    BOOST_CHECK(tx.vout[1].IsFee());
    BOOST_CHECK(tx.vout[1].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[1].nValue.GetAmount(), 10000);
    BOOST_CHECK(tx.vout[1].vchRangeproof.empty());

    CAmount a0 = -1, a2 = -1;
    uint256 b0, b2;
    BOOST_REQUIRE(UnblindValue(tx.vout[0].nValue, tx.vout[0].nNonce,
                               tx.vout[0].vchRangeproof, a0, b0));
    BOOST_REQUIRE(UnblindValue(tx.vout[2].nValue, tx.vout[2].nNonce,
                               tx.vout[2].vchRangeproof, a2, b2));
    BOOST_CHECK_EQUAL(a0, 500000);
    BOOST_CHECK_EQUAL(a2, 250000);
}

// Last output is fee: must remain explicit (not turned into a commitment).
BOOST_AUTO_TEST_CASE(blind_tx_last_output_is_fee)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(800000));
    tx.vout.push_back(MakeFeeOut(12345));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds;
    std::vector<uint256> out_nonces;
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces));

    BOOST_CHECK(tx.vout[1].IsFee());
    BOOST_CHECK(tx.vout[1].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[1].nValue.GetAmount(), 12345);

    CAmount a0 = -1;
    uint256 b0;
    BOOST_REQUIRE(UnblindValue(tx.vout[0].nValue, tx.vout[0].nNonce,
                               tx.vout[0].vchRangeproof, a0, b0));
    BOOST_CHECK_EQUAL(a0, 800000);
}

// Single fee-only tx: current API returns true and leaves the fee explicit.
BOOST_AUTO_TEST_CASE(blind_tx_fee_only)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeFeeOut(999));

    std::vector<uint256> in_blinds;
    std::vector<uint256> out_blinds;
    std::vector<uint256> out_nonces;
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces));
    BOOST_CHECK(tx.vout[0].IsFee());
    BOOST_CHECK(tx.vout[0].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[0].nValue.GetAmount(), 999);
}

// Single CT output with no input blinds: balances itself, still rewinds.
BOOST_AUTO_TEST_CASE(blind_tx_single_ct_no_input_blinds)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(1000));
    std::vector<uint256> in_blinds;
    std::vector<uint256> out_blinds, out_nonces;
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces));
    CAmount a = -1;
    uint256 b;
    BOOST_REQUIRE(UnblindValue(tx.vout[0].nValue, tx.vout[0].nNonce,
                               tx.vout[0].vchRangeproof, a, b));
    BOOST_CHECK_EQUAL(a, 1000);
}

BOOST_AUTO_TEST_CASE(blind_tx_empty_vout)
{
    CMutableTransaction tx;
    std::vector<uint256> in_blinds;
    std::vector<uint256> out_blinds;
    std::vector<uint256> out_nonces;
    BOOST_CHECK(!BlindTransaction(in_blinds, tx, out_blinds, out_nonces));
}

// All outputs explicit (fee/plaintext) with a confidential input: there is no
// blinded output to carry the input's blinding factor, so the Pedersen tally
// could never balance and the tx would be rejected at consensus
// (bad-txns-ct-balance). Fail closed at creation instead.
BOOST_AUTO_TEST_CASE(blind_tx_all_explicit_with_ct_input_fails)
{
    // A spent confidential coin carries a non-zero blinding factor.
    uint256 ct_input_blind;
    ct_input_blind.begin()[0] = 1;
    std::vector<uint256> in_blinds = {ct_input_blind};

    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(900000));
    tx.vout.push_back(MakeExplicitOut(700000));

    std::vector<uint256> out_blinds, out_nonces;
    std::vector<std::optional<CPubKey>> keys = {std::nullopt, std::nullopt};
    std::vector<bool> keep_explicit = {true, true};
    BOOST_CHECK(!BlindTransaction(in_blinds, tx, out_blinds, out_nonces, keys, &keep_explicit));

    // Same shape with a zero (explicit) input blind must be accepted: the
    // plaintext outputs carry no commitment, so the tally trivially balances.
    std::vector<uint256> zero_in_blinds(1);
    BOOST_REQUIRE(BlindTransaction(zero_in_blinds, tx, out_blinds, out_nonces, keys, &keep_explicit));
    BOOST_CHECK(tx.vout[0].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[0].nValue.GetAmount(), 900000);
    BOOST_CHECK(tx.vout[1].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[1].nValue.GetAmount(), 700000);
    BOOST_CHECK(tx.vout[0].nNonce.vchCommitment.empty());
    BOOST_CHECK(tx.vout[1].vchRangeproof.empty());
    BOOST_CHECK(out_blinds[0] == uint256());
    BOOST_CHECK(out_blinds[1] == uint256());
}

// Default path B (recipient ECDH): when every output carries a recipient
// public key, the nonce commitment holds an ephemeral compressed pubkey
// (odd-Y, 0x03 prefix) instead of the path-A plaintext nonce. A key-less
// UnblindValue / GetNonce must fail closed, while each recipient's own
// private key recovers the exact amount and blinding factor.
//
// Amounts follow the fixed vector for this test family
// (B1: pay + change) and deliberately differ from the legacy path-A cases
// above so the two suites never share numbers: amount_B = 123456789,
// change_amount = 50000000, fee_explicit = 10000.
BOOST_AUTO_TEST_CASE(blind_tx_path_b_recipient_key)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    BOOST_REQUIRE(recv_key.IsValid());
    const CPubKey recv_pub = recv_key.GetPubKey();

    CKey change_key;
    change_key.MakeNewKey(true);
    BOOST_REQUIRE(change_key.IsValid());
    const CPubKey change_pub = change_key.GetPubKey();

    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));
    tx.vout.push_back(MakeExplicitOut(50000000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;
    std::vector<std::optional<CPubKey>> recipient_keys = {recv_pub, change_pub};
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, recipient_keys));
    BOOST_REQUIRE_EQUAL(out_blinds.size(), 2U);

    for (size_t i = 0; i < tx.vout.size(); ++i) {
        BOOST_CHECK(!tx.vout[i].IsFee());
        BOOST_CHECK(tx.vout[i].nValue.IsCommitment());

        // Path B marker: 33-byte ephemeral pubkey with 0x03 (odd-Y) prefix,
        // never the 0x02 plaintext-nonce legacy shape. A path-A decoder
        // (0x02-only, IsLegacyNonceCommit) cannot treat this as a raw nonce.
        BOOST_CHECK_EQUAL(tx.vout[i].nNonce.vchCommitment.size(), 33U);
        BOOST_CHECK_EQUAL(tx.vout[i].nNonce.vchCommitment[0], 0x03);

        // A key-less observer cannot decode the nonce or rewind the amount.
        BOOST_CHECK(GetNonce(tx.vout[i].nNonce).IsNull());
        CAmount amt = -1;
        uint256 b;
        BOOST_CHECK(!UnblindValue(tx.vout[i].nValue, tx.vout[i].nNonce,
                                  tx.vout[i].vchRangeproof, amt, b));
        // Failed rewind must surface as nullopt, never as a "zero" amount.
        BOOST_CHECK(!GetOutputAmount(tx.vout[i]).has_value());

        // Each recipient's own private key recovers the exact amount + blind.
        const CKey& own_key = (i == 0 ? recv_key : change_key);
        const CAmount expected = (i == 0 ? 123456789 : 50000000);
        CAmount recovered = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValueWithKey(own_key, tx.vout[i].nValue, tx.vout[i].nNonce,
                                          tx.vout[i].vchRangeproof, recovered, blind_out));
        BOOST_CHECK_EQUAL(recovered, expected);
        BOOST_CHECK(blind_out == out_blinds[i]);

        // A wrong private key (B2) must not unblind, and must not clobber the
        // output amount with bogus data.
        CKey wrong_key;
        wrong_key.MakeNewKey(true);
        CAmount wrong_recovered = -1;
        uint256 wrong_blind;
        BOOST_CHECK(!UnblindValueWithKey(wrong_key, tx.vout[i].nValue, tx.vout[i].nNonce,
                                         tx.vout[i].vchRangeproof, wrong_recovered, wrong_blind));
        BOOST_CHECK_EQUAL(wrong_recovered, -1);
    }
}

// Path-B hardening (mainnet default send path): once a key list is engaged,
// a non-fee output whose recipient public key cannot be resolved (nullopt)
// and which was not explicitly marked to stay plaintext must fail the whole
// transaction (fail closed) instead of silently downgrading to the
// plaintext-nonce path A. An output explicitly marked plaintext is emitted
// unblinded (explicit value, no nonce, no range proof). A length mismatch
// still aborts the transaction.
BOOST_AUTO_TEST_CASE(blind_tx_path_b_missing_key_fails_closed)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;

    // Engaged key list but this output has no key and no explicit marker:
    // the transaction must be rejected, never degraded to path A.
    std::vector<std::optional<CPubKey>> keys_with_nullopt = {std::nullopt};
    BOOST_CHECK(!BlindTransaction(in_blinds, tx, out_blinds, out_nonces, keys_with_nullopt));

    // The same output explicitly marked to stay plaintext is emitted
    // unblinded: explicit value, empty nonce, empty range proof.
    std::vector<bool> keep_explicit = {true};
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, keys_with_nullopt, &keep_explicit));
    BOOST_CHECK(tx.vout[0].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[0].nValue.GetAmount(), 123456789);
    BOOST_CHECK(tx.vout[0].nNonce.vchCommitment.empty());
    BOOST_CHECK(tx.vout[0].vchRangeproof.empty());

    // Key-list length does not line up with the outputs: rejected.
    CMutableTransaction tx2;
    tx2.vout.push_back(MakeExplicitOut(123456789));
    tx2.vout.push_back(MakeExplicitOut(50000000));
    std::vector<std::optional<CPubKey>> keys_too_short = {std::nullopt};
    BOOST_CHECK(!BlindTransaction(in_blinds, tx2, out_blinds, out_nonces, keys_too_short));
}

// Keyed outputs stay path B while a key-less plain rcpu1... output is emitted
// as a plaintext (explicit-value) output in the same transaction.
BOOST_AUTO_TEST_CASE(blind_tx_path_b_mixed_keyed_and_plaintext)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    const CPubKey recv_pub = recv_key.GetPubKey();

    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));
    tx.vout.push_back(MakeExplicitOut(50000000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;
    std::vector<std::optional<CPubKey>> recipient_keys = {recv_pub, std::nullopt};
    std::vector<bool> keep_explicit = {false, true};
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, recipient_keys, &keep_explicit));

    // Output 0 (keyed): path B shape, only the recipient's private key works.
    BOOST_CHECK_EQUAL(tx.vout[0].nNonce.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(tx.vout[0].nNonce.vchCommitment[0], 0x03);
    CAmount path_b_amt = -1;
    uint256 path_b_blind;
    BOOST_CHECK(!UnblindValue(tx.vout[0].nValue, tx.vout[0].nNonce,
                              tx.vout[0].vchRangeproof, path_b_amt, path_b_blind));
    BOOST_REQUIRE(UnblindValueWithKey(recv_key, tx.vout[0].nValue, tx.vout[0].nNonce,
                                      tx.vout[0].vchRangeproof, path_b_amt, path_b_blind));
    BOOST_CHECK_EQUAL(path_b_amt, 123456789);

    // Output 1 (key-less, plaintext): explicit value, no nonce, no range
    // proof, zero blinding factor.
    BOOST_CHECK(tx.vout[1].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[1].nValue.GetAmount(), 50000000);
    BOOST_CHECK(tx.vout[1].nNonce.vchCommitment.empty());
    BOOST_CHECK(tx.vout[1].vchRangeproof.empty());
    BOOST_CHECK(out_blinds[1] == uint256());
}

// Fee outputs are exempt: they carry no commitment and need no key. A key
// list may carry a nullopt slot on the fee position as long as the length
// matches and every non-fee output has a key. The fee stays explicit
// (fee_explicit = 10000) and is not part of the "must have a key" set.
BOOST_AUTO_TEST_CASE(blind_tx_path_b_fee_exempt)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    const CPubKey recv_pub = recv_key.GetPubKey();

    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));
    tx.vout.push_back(MakeFeeOut(10000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;
    std::vector<std::optional<CPubKey>> recipient_keys = {recv_pub, std::nullopt};
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, recipient_keys));

    BOOST_CHECK(tx.vout[1].IsFee());
    BOOST_CHECK(tx.vout[1].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[1].nValue.GetAmount(), 10000);

CAmount recovered = -1;
    uint256 blind_out;
    BOOST_REQUIRE(UnblindValueWithKey(recv_key, tx.vout[0].nValue, tx.vout[0].nNonce,
                                      tx.vout[0].vchRangeproof, recovered, blind_out));
    BOOST_CHECK_EQUAL(recovered, 123456789);
    BOOST_CHECK(blind_out == out_blinds[0]);
}

// Path C is the default wallet send path: with use_path_c=true every keyed
// non-fee output (payment to rcpux1..., change) is blinded with the 0x04 || X
// nonce commitment and the HKDF rewind nonce (doc/ct-path-c.md). Key-less
// outputs stay explicit plaintext; fee outputs stay exempt; the amount is
// recoverable via UnblindValueWithKeyV2 (scriptPubKey in the HKDF info domain)
// and must not be readable through the Path B decoder.
BOOST_AUTO_TEST_CASE(blind_tx_path_c_recipient_key)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    BOOST_REQUIRE(recv_key.IsValid());
    const CPubKey recv_pub = recv_key.GetPubKey();

    CKey change_key;
    change_key.MakeNewKey(true);
    BOOST_REQUIRE(change_key.IsValid());
    const CPubKey change_pub = change_key.GetPubKey();

    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));
    tx.vout.push_back(MakeExplicitOut(50000000));
    tx.vout.push_back(MakeExplicitOut(70000000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;
    std::vector<std::optional<CPubKey>> recipient_keys = {recv_pub, change_pub, std::nullopt};
    std::vector<bool> keep_explicit = {false, false, true};
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, recipient_keys, &keep_explicit,
                                   /*use_path_c=*/true));
    BOOST_REQUIRE_EQUAL(out_blinds.size(), 3U);

    // Outputs 0/1 (keyed): Path C shape -- 33-byte nonce commitment with the
    // 0x04 prefix, X-only ephemeral. Never legacy-shaped, never Path B.
    for (size_t i = 0; i < 2; ++i) {
        BOOST_CHECK(!tx.vout[i].IsFee());
        BOOST_CHECK(tx.vout[i].nValue.IsCommitment());
        BOOST_CHECK_EQUAL(tx.vout[i].nNonce.vchCommitment.size(), 33U);
        BOOST_CHECK_EQUAL(tx.vout[i].nNonce.vchCommitment[0], 0x04);
        BOOST_CHECK(IsPathCNonceCommit(tx.vout[i].nNonce));
        BOOST_CHECK(!IsLegacyNonceCommit(tx.vout[i].nNonce));

        // A key-less observer cannot decode the nonce or rewind the amount.
        CAmount amt = -1;
        uint256 b;
        BOOST_CHECK(!UnblindValue(tx.vout[i].nValue, tx.vout[i].nNonce,
                                  tx.vout[i].vchRangeproof, amt, b));
        // The Path B decoder must not rewind a Path C output either.
        const CKey& own_key = (i == 0 ? recv_key : change_key);
        const CAmount expected = (i == 0 ? 123456789 : 50000000);
        CAmount b_recovered = -1;
        uint256 b_blind;
        BOOST_CHECK(!UnblindValueWithKey(own_key, tx.vout[i].nValue, tx.vout[i].nNonce,
                                         tx.vout[i].vchRangeproof, b_recovered, b_blind));
        BOOST_CHECK_EQUAL(b_recovered, -1);

        // The Path C decoder recovers the exact amount + blind, with the
        // scriptPubKey bound into the HKDF info domain.
        CAmount c_recovered = -1;
        uint256 c_blind;
        BOOST_REQUIRE(UnblindValueWithKeyV2(own_key, tx.vout[i].nValue, tx.vout[i].nNonce,
                                            tx.vout[i].vchRangeproof, tx.vout[i].scriptPubKey,
                                            c_recovered, c_blind));
        BOOST_CHECK_EQUAL(c_recovered, expected);
        BOOST_CHECK(c_blind == out_blinds[i]);

        // Wrong key (B2) must not unblind, and must not clobber the amount.
        CKey wrong_key;
        wrong_key.MakeNewKey(true);
        CAmount wrong_recovered = -1;
        uint256 wrong_blind;
        BOOST_CHECK(!UnblindValueWithKeyV2(wrong_key, tx.vout[i].nValue, tx.vout[i].nNonce,
                                           tx.vout[i].vchRangeproof, tx.vout[i].scriptPubKey,
                                           wrong_recovered, wrong_blind));
        BOOST_CHECK_EQUAL(wrong_recovered, -1);
        // A wrong scriptPubKey must not unblind either (it is bound in info).
        CAmount spk_recovered = -1;
        uint256 spk_blind;
        CScript other_spk = CScript() << OP_TRUE << OP_1;
        BOOST_CHECK(!UnblindValueWithKeyV2(own_key, tx.vout[i].nValue, tx.vout[i].nNonce,
                                           tx.vout[i].vchRangeproof, other_spk,
                                           spk_recovered, spk_blind));
        BOOST_CHECK_EQUAL(spk_recovered, -1);
    }

    // Output 2 (key-less, marked explicit): plaintext value, no nonce, no
    // range proof, zero blinding factor.
    BOOST_CHECK(tx.vout[2].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[2].nValue.GetAmount(), 70000000);
    BOOST_CHECK(tx.vout[2].nNonce.vchCommitment.empty());
    BOOST_CHECK(tx.vout[2].vchRangeproof.empty());
    BOOST_CHECK(out_blinds[2] == uint256());
}

// Appendix A of doc/ct-path-c.md: replay vector B1 through the repository's
// own ComputeECDHNonce (declared in blind.h). ss must be byte-identical in
// both directions (sender ephem x recipient_pub, receiver priv x ephem_pub),
// the nonce commitment must carry the 0x03-prefixed ephemeral pubkey, and a
// key-less Path A decoder must fail closed (no garbage amount). Also asserts
// the negative case: flipping the prefix to 0x02 must not let the X bytes be
// reinterpreted as a Path A raw nonce. Any future Path C patch must keep
// these values byte-identical or historical Path B UTXOs stop unblinding.
BOOST_AUTO_TEST_CASE(path_b_vector_b1_compute_ecdh_nonce)
{
    // Deterministic scalars from the frozen vector: parallel to how
    // blind_tx_path_b_recipient_key builds outputs, but with fixed keys so
    // the ECDH shared secret itself is pinned.
    const auto recipient_priv_hex = ParseHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    const auto recipient_pub_hex = ParseHex("0284bf7562262bbd6940085748f3be6afa52ae317155181ece31b66351ccffa4b0");
    const auto ephemeral_priv_hex = ParseHex("2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f42");
    const auto ephemeral_pub_hex = ParseHex("0338be4e8cfa078d48299b557033a07024a46963c874c666f0cfb9b753425da2cc");

    CKey recipient_priv;
    recipient_priv.Set(recipient_priv_hex.begin(), recipient_priv_hex.end(), true);
    BOOST_REQUIRE(recipient_priv.IsValid());
    const CPubKey recipient_pub{recipient_pub_hex.begin(), recipient_pub_hex.end()};
    BOOST_REQUIRE(recipient_pub.IsValid());

    CKey ephemeral_priv;
    ephemeral_priv.Set(ephemeral_priv_hex.begin(), ephemeral_priv_hex.end(), true);
    BOOST_REQUIRE(ephemeral_priv.IsValid());
    const CPubKey ephemeral_pub{ephemeral_pub_hex.begin(), ephemeral_pub_hex.end()};
    BOOST_REQUIRE(ephemeral_pub.IsValid());

    // Sanity: the frozen private keys derive the frozen public keys, so the
    // vector is internally consistent.
    BOOST_CHECK(recipient_priv.GetPubKey() == recipient_pub);
    BOOST_CHECK(ephemeral_priv.GetPubKey() == ephemeral_pub);

    // 1) Sender side: ss = ComputeECDHNonce(ephemeral_priv, recipient_pub).
    // The frozen ss is the raw big-endian X coordinate: CopyX32 copies the
    // shared-point X verbatim into the uint256 buffer, so compare byte-exact
    // (a uint256S comparison would read the buffer in reverse byte order).
    uint256 ss_sender;
    BOOST_REQUIRE(ComputeECDHNonce(ephemeral_priv, recipient_pub, ss_sender));
    const auto ss_expected = ParseHex("8ff9563819af439784eff54bd65e65614b14c7fe66b6b2f6010a72ec681f38f0");
    BOOST_REQUIRE_EQUAL(ss_expected.size(), 32U);
    BOOST_CHECK(std::equal(ss_sender.begin(), ss_sender.end(), ss_expected.begin()));

    // 2) Receiver side: ss = ComputeECDHNonce(recipient_priv, ephemeral_pub),
    // must be the very same 32 bytes (ECDH symmetry).
    uint256 ss_receiver;
    BOOST_REQUIRE(ComputeECDHNonce(recipient_priv, ephemeral_pub, ss_receiver));
    BOOST_CHECK(ss_receiver == ss_sender);
    BOOST_CHECK(std::equal(ss_receiver.begin(), ss_receiver.end(), ss_expected.begin()));

    // 6) The on-chain nonce commitment must be the frozen ephemeral pubkey:
    // 33 bytes, 0x03 prefix, and never legacy-shaped.
    CConfidentialNonce nonce_commit;
    nonce_commit.vchCommitment.assign(ephemeral_pub_hex.begin(), ephemeral_pub_hex.end());
    BOOST_CHECK_EQUAL(nonce_commit.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(nonce_commit.vchCommitment[0], 0x03);
    BOOST_CHECK(!IsLegacyNonceCommit(nonce_commit));

    // 3-5) Full round trip with the frozen recipient key and the frozen
    // amount: BlindOutputToRecipient -> UnblindValueWithKey(recipient_priv)
    // recovers the exact amount (assertion 3); a key-less UnblindValue fails
    // (assertion 4) and GetOutputAmount is nullopt, never a zero amount
    // (assertion 5).
    const CAmount amount = 500000000;
    CConfidentialValue conf_value;
    CConfidentialNonce nc;
    std::vector<unsigned char> rangeproof;
    uint256 out_blind;
    BOOST_REQUIRE(BlindOutputToRecipient(conf_value, nc, rangeproof, out_blind, amount, recipient_pub));

    CAmount recovered = -1;
    uint256 recovered_blind;
    BOOST_REQUIRE(UnblindValueWithKey(recipient_priv, conf_value, nc, rangeproof, recovered, recovered_blind));
    BOOST_CHECK_EQUAL(recovered, amount);
    BOOST_CHECK(recovered_blind == out_blind);

    CAmount keyless = -1;
    uint256 keyless_blind;
    BOOST_CHECK(!UnblindValue(conf_value, nc, rangeproof, keyless, keyless_blind));
    CTxOut txout;
    txout.nValue = conf_value;
    txout.nNonce = nc;
    txout.vchRangeproof = rangeproof;
    BOOST_CHECK(!GetOutputAmount(txout).has_value());

    // Negative: flipping the prefix to 0x02 must not turn the ephemeral
    // pubkey's X coordinate into a usable Path A raw nonce. The commitment is
    // legacy-shaped on the surface (GetNonce decodes the X bytes), but the
    // rewind must still fail: the X coordinate is not the nonce the range
    // proof was signed with, so a key-less decoder gets no amount.
    CConfidentialNonce flipped = nonce_commit;
    flipped.vchCommitment[0] = 0x02;
    BOOST_CHECK(IsLegacyNonceCommit(flipped));
    BOOST_CHECK(!GetNonce(flipped).IsNull());
    CAmount amt = -1;
    uint256 blind2;
    BOOST_CHECK(!UnblindValue(conf_value, flipped, rangeproof, amt, blind2));
    BOOST_CHECK_EQUAL(amt, -1);
CTxOut flipped_out = txout;
    flipped_out.nNonce = flipped;
    BOOST_CHECK(!GetOutputAmount(flipped_out).has_value());
}

// -------------------------------------------------------------------------
// Path C (0x04 || X nonce, HKDF rewind nonce). Wallet-layer only, off by
// default: BlindOutputToRecipientV2 / UnblindValueWithKeyV2 exist for the
// upcoming rcpux1 (Path C) default switch; the current default send path
// (Path B / explicit) is untouched by this patch.
// -------------------------------------------------------------------------

namespace {
// A realistic non-trivial scriptPubKey (22-byte P2WPKH-shaped) to bind into
// the Path C HKDF info domain.
CScript PathCTestScriptPubKey()
{
    return CScript() << OP_0 << std::vector<unsigned char>(20, 0x42);
}
} // namespace

// Path C sender -> recipient round trip through the V2 API: the commitment is
// 0x04 || X (never legacy-shaped), the recipient spend key rewinds the exact
// amount and blinding factor, and a key-less Path A decoder fails closed.
BOOST_AUTO_TEST_CASE(path_c_recipient_roundtrip_v2)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    BOOST_REQUIRE(recv_key.IsValid());
    const CPubKey recv_pub = recv_key.GetPubKey();
    const CScript spk = PathCTestScriptPubKey();

    const CAmount amount = 43210;
    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;

    BOOST_REQUIRE(BlindOutputToRecipientV2(conf_value, nonce_commit, rangeproof, blind, amount, recv_pub, spk));
    BOOST_REQUIRE_EQUAL(nonce_commit.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(nonce_commit.vchCommitment[0], 0x04);
    BOOST_CHECK(IsPathCNonceCommit(nonce_commit));
    // 0x04 must never be mistaken for a legacy plaintext nonce.
    BOOST_CHECK(!IsLegacyNonceCommit(nonce_commit));
    BOOST_CHECK(GetNonce(nonce_commit).IsNull());

    CAmount amount_out = -1;
    uint256 blind_out;
    BOOST_REQUIRE(UnblindValueWithKeyV2(recv_key, conf_value, nonce_commit, rangeproof, spk, amount_out, blind_out));
    BOOST_CHECK_EQUAL(amount_out, amount);
    BOOST_CHECK(blind_out == blind);

    // A key-less Path A rewind on a Path C commitment must fail closed, never
    // conflating failure with a zero amount.
    CAmount keyless = -1;
    uint256 keyless_blind;
    BOOST_CHECK(!UnblindValue(conf_value, nonce_commit, rangeproof, keyless, keyless_blind));
    BOOST_CHECK_EQUAL(keyless, -1);
    CTxOut txout;
    txout.nValue = conf_value;
    txout.nNonce = nonce_commit;
    txout.vchRangeproof = rangeproof;
    BOOST_CHECK(!GetOutputAmount(txout).has_value());
}

// X-only recovery covers both Y parities: a Path C nonce carries X only, and
// ReconstructPathCEphemeral reconstructs a key with the SAME X regardless of
// the sender's parity. For a valid curve X both 02||X and 03||X parse (x^3+7
// is a QR), so recovery deterministically yields the even-Y point; the Y flip
// is harmless because the rewind nonce is CopyX32(ECDH(...)), which reads only
// the X coordinate (see path_c_view_seed_rewinds_sender_nonce / doc
// 7b check). Each output draws a fresh ephemeral, so repeated sends must
// produce distinct nonce commitments.
BOOST_AUTO_TEST_CASE(path_c_even_odd_y_recovery_and_fresh_ephemeral)
{
    // Feed 0x04 || X built from a known even-Y key and a known odd-Y key
    // directly into the recovery path: both must come back with the same X.
    for (unsigned char desired_prefix : {static_cast<unsigned char>(0x02), static_cast<unsigned char>(0x03)}) {
        CKey eph_key;
        CPubKey eph_pub;
        for (int tries = 0; tries < 100; ++tries) {
            eph_key.MakeNewKey(true);
            eph_pub = eph_key.GetPubKey();
            if (eph_pub.size() == 33 && eph_pub[0] == desired_prefix) break;
        }
        BOOST_REQUIRE_EQUAL(eph_pub.size(), 33U);
        BOOST_REQUIRE_EQUAL(eph_pub[0], desired_prefix);

        CConfidentialNonce nc;
        nc.vchCommitment.resize(33);
        nc.vchCommitment[0] = 0x04;
        std::copy(eph_pub.begin() + 1, eph_pub.end(), nc.vchCommitment.begin() + 1);

        CPubKey recovered;
        BOOST_REQUIRE(ReconstructPathCEphemeral(nc, recovered));
        BOOST_REQUIRE(recovered.IsValid());
        BOOST_REQUIRE_EQUAL(recovered.size(), 33U);
        BOOST_CHECK_EQUAL_COLLECTIONS(&nc.vchCommitment[1], &nc.vchCommitment[1] + 32,
                                      &recovered[1], &recovered[1] + 32);
    }

    // Spend-side round trip: the recipient's spend key rewinds the exact
    // amount and blinding factor through the V2 entry point.
    {
        CKey recv_key;
        recv_key.MakeNewKey(true);
        const CPubKey recv_pub = recv_key.GetPubKey();
        const CScript spk = PathCTestScriptPubKey();

        CConfidentialValue conf_value;
        CConfidentialNonce nonce_commit;
        std::vector<unsigned char> rangeproof;
        uint256 blind;
        BOOST_REQUIRE(BlindOutputToRecipientV2(conf_value, nonce_commit, rangeproof, blind, 1 * COIN, recv_pub, spk));
        BOOST_REQUIRE(IsPathCNonceCommit(nonce_commit));

        CAmount amount_out = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValueWithKeyV2(recv_key, conf_value, nonce_commit, rangeproof, spk, amount_out, blind_out));
        BOOST_CHECK_EQUAL(amount_out, 1 * COIN);
        BOOST_CHECK(blind_out == blind);
    }

    // Fresh ephemeral per output: two sends to the same recipient must carry
    // different 0x04 || X payloads.
    CKey recv_key;
    recv_key.MakeNewKey(true);
    const CPubKey recv_pub = recv_key.GetPubKey();
    const CScript spk = PathCTestScriptPubKey();
    CConfidentialValue c1, c2;
    CConfidentialNonce n1, n2;
    std::vector<unsigned char> p1, p2;
    uint256 b1, b2;
    BOOST_REQUIRE(BlindOutputToRecipientV2(c1, n1, p1, b1, 2 * COIN, recv_pub, spk));
    BOOST_REQUIRE(BlindOutputToRecipientV2(c2, n2, p2, b2, 2 * COIN, recv_pub, spk));
    BOOST_CHECK(n1.vchCommitment != n2.vchCommitment);
}

// Malformed Path C nonce commitments must be rejected before any key
// reconstruction: wrong length, wrong prefix, or an X with no valid Y.
BOOST_AUTO_TEST_CASE(path_c_reconstruct_rejects_malformed)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    const CPubKey recv_pub = recv_key.GetPubKey();
    const CScript spk = PathCTestScriptPubKey();

    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;
    BOOST_REQUIRE(BlindOutputToRecipientV2(conf_value, nonce_commit, rangeproof, blind, 3 * COIN, recv_pub, spk));

    // Wrong length (32 or 34 bytes): never Path C shaped.
    CConfidentialNonce short_nc = nonce_commit;
    short_nc.vchCommitment.pop_back();
    BOOST_CHECK(!IsPathCNonceCommit(short_nc));
    CPubKey out;
    BOOST_CHECK(!ReconstructPathCEphemeral(short_nc, out));

    // Plaintext-nonce prefix 0x02 and Path B prefix 0x03 are not Path C.
    CConfidentialNonce swapped = nonce_commit;
    swapped.vchCommitment[0] = 0x02;
    BOOST_CHECK(!IsPathCNonceCommit(swapped));
    BOOST_CHECK(!ReconstructPathCEphemeral(swapped, out));
    swapped.vchCommitment[0] = 0x03;
    BOOST_CHECK(!IsPathCNonceCommit(swapped));
    BOOST_CHECK(!ReconstructPathCEphemeral(swapped, out));

    // An unknown prefix must fail the V2 unblind dispatch, not fall back.
    CConfidentialNonce unknown = nonce_commit;
    unknown.vchCommitment[0] = 0x05;
    CAmount amount_out = -1;
    uint256 blind_out;
    BOOST_CHECK(!UnblindValueWithKeyV2(recv_key, conf_value, unknown, rangeproof, spk, amount_out, blind_out));
    BOOST_CHECK_EQUAL(amount_out, -1);
}

// View-key rewind == sender nonce, by construction: both share the same HKDF
// Extract PRK (view_seed = HMAC("rcpu-pathc-v1", ss)) and the same Expand
// info (scriptPubKey || 0x01 || le64(64)). Replays the frozen B1 ss so the
// derivation itself is pinned, not just the ECDH step.
BOOST_AUTO_TEST_CASE(path_c_view_seed_rewinds_sender_nonce)
{
    // Frozen B1 vector (Appendix A): recipient and ephemeral keys plus the
    // pinned shared secret, replayed through the repo's own ComputeECDHNonce.
    const auto recipient_priv_hex = ParseHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    const auto recipient_pub_hex = ParseHex("0284bf7562262bbd6940085748f3be6afa52ae317155181ece31b66351ccffa4b0");
    const auto ephemeral_priv_hex = ParseHex("2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f42");
    const auto ss_expected = ParseHex("8ff9563819af439784eff54bd65e65614b14c7fe66b6b2f6010a72ec681f38f0");

    CKey recipient_priv;
    recipient_priv.Set(recipient_priv_hex.begin(), recipient_priv_hex.end(), true);
    BOOST_REQUIRE(recipient_priv.IsValid());
    const CPubKey recipient_pub{recipient_pub_hex.begin(), recipient_pub_hex.end()};
    BOOST_REQUIRE(recipient_pub.IsValid());
    CKey ephemeral_priv;
    ephemeral_priv.Set(ephemeral_priv_hex.begin(), ephemeral_priv_hex.end(), true);
    BOOST_REQUIRE(ephemeral_priv.IsValid());

    uint256 ss;
    BOOST_REQUIRE(ComputeECDHNonce(ephemeral_priv, recipient_pub, ss));
    BOOST_REQUIRE_EQUAL(ss_expected.size(), 32U);
    BOOST_CHECK(std::equal(ss.begin(), ss.end(), ss_expected.begin()));

    const CScript spk = PathCTestScriptPubKey();

    // Sender nonce.
    uint256 sender_nonce;
    BOOST_REQUIRE(DerivePathCNonce(ss, spk, sender_nonce));

    // View seed and its rewind nonce.
    uint256 view_seed;
    BOOST_REQUIRE(DeriveViewSeed(ss, spk, view_seed));
    uint256 view_nonce;
    BOOST_REQUIRE(DeriveRewindNonceFromViewSeed(view_seed, spk, view_nonce));

    // The whole point of the PRK construction: byte-identical nonces.
    BOOST_CHECK(std::equal(view_nonce.begin(), view_nonce.end(), sender_nonce.begin()));

    // The scriptPubKey is bound in the Expand info: a different script must
    // produce a different rewind nonce from the same view_seed.
    const CScript other_spk = CScript() << OP_0 << std::vector<unsigned char>(20, 0x99);
    uint256 other_nonce;
    BOOST_REQUIRE(DeriveRewindNonceFromViewSeed(view_seed, other_spk, other_nonce));
    BOOST_CHECK(!(other_nonce == view_nonce));

    // A full view-side unblind of a real Path C output: the view seed holder
    // (no spend key, no ECDH) recovers the exact amount.
    const CAmount amount = 987654321;
    CConfidentialValue conf_value;
    CConfidentialNonce nonce_commit;
    std::vector<unsigned char> rangeproof;
    uint256 blind;
    BOOST_REQUIRE(BlindOutputToRecipientV2(conf_value, nonce_commit, rangeproof, blind, amount, recipient_pub, spk));
    BOOST_REQUIRE(IsPathCNonceCommit(nonce_commit));
    CPubKey ephemeral_pub;
    BOOST_REQUIRE(ReconstructPathCEphemeral(nonce_commit, ephemeral_pub));
    uint256 view_ss;
    BOOST_REQUIRE(ComputeECDHNonce(recipient_priv, ephemeral_pub, view_ss));
    uint256 view_seed2;
    BOOST_REQUIRE(DeriveViewSeed(view_ss, spk, view_seed2));
    uint256 view_nonce2;
    BOOST_REQUIRE(DeriveRewindNonceFromViewSeed(view_seed2, spk, view_nonce2));

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    BOOST_REQUIRE(ctx != nullptr);
    secp256k1_pedersen_commitment commit;
    BOOST_REQUIRE(secp256k1_pedersen_commitment_parse(ctx, &commit, conf_value.vchCommitment.data()) == 1);
    uint64_t value = 0;
    uint64_t min_value = 0, max_value = 0;
    uint256 blind_out;
    const int rc = secp256k1_rangeproof_rewind(ctx, blind_out.begin(), &value, nullptr, nullptr, view_nonce2.begin(),
                                               &min_value, &max_value, &commit, rangeproof.data(), rangeproof.size(),
                                               nullptr, 0, secp256k1_generator_h);
    secp256k1_context_destroy(ctx);
    BOOST_CHECK_EQUAL(rc, 1);
    BOOST_CHECK_EQUAL(static_cast<CAmount>(value), amount);
    BOOST_CHECK(blind_out == blind);
}

// V2 unblind dispatch: 0x03 stays Path B (CopyX32), 0x02 stays Path A
// (plaintext nonce), so the new entry point is a superset of the old ones and
// the historical paths keep working verbatim.
BOOST_AUTO_TEST_CASE(path_c_unblind_v2_dispatch_preserves_legacy)
{
    const CScript spk = PathCTestScriptPubKey();

    // Path B output: 0x03 prefix, CopyX32 nonce. UnblindValueWithKeyV2 must
    // delegate to the original behavior and ignore the extra scriptPubKey.
    {
        CKey recv_key;
        recv_key.MakeNewKey(true);
        const CPubKey recv_pub = recv_key.GetPubKey();

        const CAmount amount = 111;
        CConfidentialValue conf_value;
        CConfidentialNonce nonce_commit;
        std::vector<unsigned char> rangeproof;
        uint256 blind;
        BOOST_REQUIRE(BlindOutputToRecipient(conf_value, nonce_commit, rangeproof, blind, amount, recv_pub));
        BOOST_REQUIRE_EQUAL(nonce_commit.vchCommitment[0], 0x03);

        CAmount amount_out = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValueWithKeyV2(recv_key, conf_value, nonce_commit, rangeproof, spk, amount_out, blind_out));
        BOOST_CHECK_EQUAL(amount_out, amount);
        BOOST_CHECK(blind_out == blind);
    }

    // Path A output: 0x02 prefix, plaintext nonce. UnblindValueWithKeyV2 must
    // delegate to the key-less rewind (UnblindValue), not treat it as Path B/C.
    {
        const CAmount amount = 222;
        CConfidentialValue conf_value;
        CConfidentialNonce nonce_commit;
        std::vector<unsigned char> rangeproof;
        uint256 blind;
        uint256 nonce;
        BOOST_REQUIRE(BlindOutput(conf_value, nonce_commit, rangeproof, blind, nonce, amount));
        BOOST_REQUIRE_EQUAL(nonce_commit.vchCommitment[0], 0x02);

        CAmount amount_out = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValueWithKeyV2(CKey(), conf_value, nonce_commit, rangeproof, spk, amount_out, blind_out));
        BOOST_CHECK_EQUAL(amount_out, amount);
        BOOST_CHECK(blind_out == blind);
    }
}

// Mixed Path C + Path A in the same tx: one keyed output (rcpux1) gets 0x04,
// one key-less output (bare rcpu1) falls through to Path A (0x02 commitment).
BOOST_AUTO_TEST_CASE(blind_tx_mixed_path_c_and_path_a)
{
    CKey recv_key;
    recv_key.MakeNewKey(true);
    BOOST_REQUIRE(recv_key.IsValid());
    const CPubKey recv_pub = recv_key.GetPubKey();

    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));
    tx.vout.push_back(MakeExplicitOut(50000000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;
    std::vector<std::optional<CPubKey>> recipient_keys = {recv_pub, std::nullopt};
    // Neither output is marked explicit: keyed → Path C, key-less → Path A.
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces,
                                   recipient_keys, /*explicit_outputs=*/nullptr,
                                   /*use_path_c=*/true));
    BOOST_REQUIRE_EQUAL(out_blinds.size(), 2U);

    // Output 0 (keyed): Path C shape, 0x04 prefix.
    BOOST_CHECK(tx.vout[0].nValue.IsCommitment());
    BOOST_CHECK(!tx.vout[0].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[0].nNonce.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(tx.vout[0].nNonce.vchCommitment[0], 0x04);
    BOOST_CHECK(IsPathCNonceCommit(tx.vout[0].nNonce));
    BOOST_CHECK(!IsLegacyNonceCommit(tx.vout[0].nNonce));

    // Recipient's own key recovers the amount via V2.
    CAmount recovered0 = -1;
    uint256 blind0;
    BOOST_REQUIRE(UnblindValueWithKeyV2(recv_key, tx.vout[0].nValue, tx.vout[0].nNonce,
                                          tx.vout[0].vchRangeproof, tx.vout[0].scriptPubKey,
                                          recovered0, blind0));
    BOOST_CHECK_EQUAL(recovered0, 123456789);
    BOOST_CHECK(blind0 == out_blinds[0]);

    // Output 1 (key-less): Path A shape, 0x02 prefix, commitment (not explicit).
    BOOST_CHECK(tx.vout[1].nValue.IsCommitment());
    BOOST_CHECK(!tx.vout[1].nValue.IsExplicit());
    BOOST_CHECK_EQUAL(tx.vout[1].nNonce.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(tx.vout[1].nNonce.vchCommitment[0], 0x02);
    BOOST_CHECK(IsLegacyNonceCommit(tx.vout[1].nNonce));

    // Path A rewind works (nonce is public).
    CAmount recovered1 = -1;
    uint256 blind1;
    BOOST_REQUIRE(UnblindValue(tx.vout[1].nValue, tx.vout[1].nNonce,
                               tx.vout[1].vchRangeproof, recovered1, blind1));
    BOOST_CHECK_EQUAL(recovered1, 50000000);
    BOOST_CHECK(blind1 == out_blinds[1]);
}

// All outputs key-less (bare rcpu1 or -ctlegacy=1): every output gets Path A
// (0x02 || nonce, commitment, rangeproof). No explicit outputs.
BOOST_AUTO_TEST_CASE(blind_tx_all_path_a_when_no_keys)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(800000));
    tx.vout.push_back(MakeExplicitOut(200000));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;
    // Empty recipient_keys = -ctlegacy=1 behaviour: whole tx Path A.
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces,
                                   /*recipient_keys=*/{},
                                   /*explicit_outputs=*/nullptr,
                                   /*use_path_c=*/true));
    BOOST_REQUIRE_EQUAL(out_blinds.size(), 2U);

    for (size_t i = 0; i < tx.vout.size(); ++i) {
        BOOST_CHECK(tx.vout[i].nValue.IsCommitment());
        BOOST_CHECK(!tx.vout[i].nValue.IsExplicit());
        BOOST_CHECK_EQUAL(tx.vout[i].nNonce.vchCommitment.size(), 33U);
        BOOST_CHECK_EQUAL(tx.vout[i].nNonce.vchCommitment[0], 0x02);
        BOOST_CHECK(IsLegacyNonceCommit(tx.vout[i].nNonce));

        CAmount recovered = -1;
        uint256 blind_out;
        BOOST_REQUIRE(UnblindValue(tx.vout[i].nValue, tx.vout[i].nNonce,
                                   tx.vout[i].vchRangeproof, recovered, blind_out));
        BOOST_CHECK_EQUAL(recovered, (i == 0 ? 800000 : 200000));
        BOOST_CHECK(blind_out == out_blinds[i]);
    }
}

BOOST_AUTO_TEST_SUITE_END()