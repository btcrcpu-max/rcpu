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

// 1.0.18 wallet unlock: once a key list is engaged, a non-fee output whose
// recipient public key cannot be resolved (nullopt) must downgrade to the
// plaintext-nonce path A for that output only, so a bare-address send never
// fails. A length mismatch still aborts the transaction.
BOOST_AUTO_TEST_CASE(blind_tx_path_b_missing_key_falls_back)
{
    CMutableTransaction tx;
    tx.vout.push_back(MakeExplicitOut(123456789));

    std::vector<uint256> in_blinds(1);
    std::vector<uint256> out_blinds, out_nonces;

    // Engaged key list but this output has no key: per-output path-A
    // fallback; the output is a valid legacy commitment and the amount must
    // be rewindable by the legacy decoder (UnblindValue).
    std::vector<std::optional<CPubKey>> keys_with_nullopt = {std::nullopt};
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, keys_with_nullopt));
    BOOST_CHECK(tx.vout[0].nValue.IsCommitment());
    BOOST_CHECK(IsLegacyNonceCommit(tx.vout[0].nNonce));
    CAmount recovered = -1;
    uint256 blind_out;
    BOOST_REQUIRE(UnblindValue(tx.vout[0].nValue, tx.vout[0].nNonce,
                               tx.vout[0].vchRangeproof, recovered, blind_out));
    BOOST_CHECK_EQUAL(recovered, 123456789);
    BOOST_CHECK(blind_out == out_blinds[0]);

    // Key-list length does not line up with the outputs: rejected.
    CMutableTransaction tx2;
    tx2.vout.push_back(MakeExplicitOut(123456789));
    tx2.vout.push_back(MakeExplicitOut(50000000));
    std::vector<std::optional<CPubKey>> keys_too_short = {std::nullopt};
    BOOST_CHECK(!BlindTransaction(in_blinds, tx2, out_blinds, out_nonces, keys_too_short));
}

// Mixed 1.0.18 behaviour: keyed outputs stay path B while the key-less
// output falls back to path A in the same transaction.
BOOST_AUTO_TEST_CASE(blind_tx_path_b_mixed_fallback)
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
    BOOST_REQUIRE(BlindTransaction(in_blinds, tx, out_blinds, out_nonces, recipient_keys));

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

    // Output 1 (key-less): legacy path A, readable without any key.
    BOOST_CHECK(IsLegacyNonceCommit(tx.vout[1].nNonce));
    CAmount path_a_amt = -1;
    uint256 path_a_blind;
    BOOST_REQUIRE(UnblindValue(tx.vout[1].nValue, tx.vout[1].nNonce,
                               tx.vout[1].vchRangeproof, path_a_amt, path_a_blind));
    BOOST_CHECK_EQUAL(path_a_amt, 50000000);
    BOOST_CHECK(path_a_blind == out_blinds[1]);
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

BOOST_AUTO_TEST_SUITE_END()