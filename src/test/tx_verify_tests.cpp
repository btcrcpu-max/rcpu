// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <blind.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <util/transaction_identifier.h>

#include <map>
#include <vector>

// Minimal in-memory CCoinsView used to feed CheckTxInputs with a
// hand-built UTXO set (one or two coins per test).
class CCoinsViewTest : public CCoinsView
{
    std::map<COutPoint, Coin> m_coins;

public:
    void Add(const COutPoint& outpoint, Coin coin)
    {
        m_coins.emplace(outpoint, std::move(coin));
    }

    bool GetCoin(const COutPoint& outpoint, Coin& coin) const override
    {
        auto it = m_coins.find(outpoint);
        if (it == m_coins.end()) {
            return false;
        }
        coin = it->second;
        return true;
    }

    uint256 GetBestBlock() const override { return uint256{}; }

    bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock, bool erase) override
    {
        for (auto& [outpoint, entry] : mapCoins) {
            if (entry.flags & CCoinsCacheEntry::DIRTY) {
                m_coins[outpoint] = entry.coin;
            }
        }
        return true;
    }
};

namespace {
/** Build a coin with an explicit (non-CT) output of the given amount. */
Coin MakeExplicitCoin(const CAmount amount, int nHeight = 100)
{
    CTxOut out;
    out.nValue.SetToAmount(amount);
    out.scriptPubKey = CScript() << OP_TRUE;
    return Coin(std::move(out), nHeight, /*fCoinBase=*/false);
}

/** Build a coin with a real Pedersen commitment (blinded) output. */
Coin MakeCommitmentCoin(const CAmount amount, int nHeight = 100)
{
    CTxOut out;
    uint256 blind, nonce;
    BOOST_REQUIRE(BlindOutput(out.nValue, out.nNonce, out.vchRangeproof, blind, nonce, amount));
    out.scriptPubKey = CScript() << OP_TRUE;
    return Coin(std::move(out), nHeight, /*fCoinBase=*/false);
}

/** A v1/v2 (non-CT) transaction spending the given outpoints to a single explicit output. */
CMutableTransaction MakeLegacySpend(const std::vector<COutPoint>& prevouts, const CAmount value_out)
{
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    for (const COutPoint& prevout : prevouts) {
        mtx.vin.emplace_back(CTxIn(prevout, CScript(), 0xffffffff));
    }
    CTxOut out;
    out.nValue.SetToAmount(value_out);
    out.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(out);
    return mtx;
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(tx_verify_tests, BasicTestingSetup)

// A legacy (v2) transaction spending an explicit output is untouched by the
// B2 rule and validates with the expected fee.
BOOST_AUTO_TEST_CASE(v2_spends_explicit_ok)
{
    const COutPoint prevout{TxidFromString("0x1111"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    const CMutableTransaction mtx = MakeLegacySpend({prevout}, 1000 * COIN);
    const CTransaction tx{mtx};

    TxValidationState state;
    CAmount txfee = -1;
    BOOST_CHECK(Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee));
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK_EQUAL(txfee, 0);
}

// B2 core rule: a legacy (v2) transaction must not spend a confidential
// (committed) output, even if the commitment would parse as "zero" amount.
BOOST_AUTO_TEST_CASE(v2_spends_commitment_rejected)
{
    const COutPoint prevout{TxidFromString("0x2222"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeCommitmentCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    const CMutableTransaction mtx = MakeLegacySpend({prevout}, 1000 * COIN);
    const CTransaction tx{mtx};

    TxValidationState state;
    CAmount txfee = -1;
    BOOST_CHECK(!Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-v2-spend-ct");
    BOOST_CHECK_EQUAL(state.GetDebugMessage(), "non-CT transaction spends a confidential output");
}

// The B2 rule fires as soon as *any* input of a legacy transaction is a
// commitment; a mixed explicit + committed input set is also rejected.
BOOST_AUTO_TEST_CASE(v2_mixed_inputs_commitment_rejected)
{
    const COutPoint prev_explicit{TxidFromString("0x3333"), 0};
    const COutPoint prev_committed{TxidFromString("0x4444"), 0};

    CCoinsViewTest base;
    base.Add(prev_explicit, MakeExplicitCoin(1000 * COIN));
    base.Add(prev_committed, MakeCommitmentCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    const CMutableTransaction mtx = MakeLegacySpend({prev_explicit, prev_committed}, 2000 * COIN);
    const CTransaction tx{mtx};

    TxValidationState state;
    CAmount txfee = -1;
    BOOST_CHECK(!Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-v2-spend-ct");
}

// The B2 rule is scoped to legacy transactions only: a v3 (CT) transaction
// spending a committed output must NOT be rejected with the B2 reason. The
// CT path decides on its own (balance / mode checks), independent of B2.
BOOST_AUTO_TEST_CASE(v3_commitment_spend_not_governed_by_b2)
{
    const COutPoint prevout{TxidFromString("0x5555"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeCommitmentCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 1000 * COIN);
    mtx.nVersion = CT_VERSION;
    const CTransaction tx{mtx};

    TxValidationState state;
    CAmount txfee = -1;
    // Whatever the CT path decides (in the default test env it reaches the
    // -ctmode gate), it must not be the B2 legacy-only reject reason.
    if (!Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee)) {
        BOOST_CHECK_NE(state.GetRejectReason(), "bad-txns-v2-spend-ct");
    } else {
        // A valid CT spend may also be accepted; either way B2 must stay out.
        BOOST_CHECK(state.IsValid());
    }
}

// A2: range proof at exactly the max size (5134) must NOT be rejected as
// "too-large".  It may still fail later (balance / verification), but the
// size gate must let it through.
BOOST_AUTO_TEST_CASE(rangeproof_exactly_max_ok)
{
    const COutPoint prevout{TxidFromString("0x6666"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 1000 * COIN);
    mtx.nVersion = CT_VERSION;
    mtx.vout[0].vchRangeproof.assign(MAX_RANGEPROOF_SIZE, 0x00);

    const CTransaction tx{mtx};
    TxValidationState state;
    CAmount txfee = -1;

    const bool old_ct_mode = g_con_elementsmode;
    g_con_elementsmode = true;
    if (!Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee)) {
        BOOST_CHECK_NE(state.GetRejectReason(), "bad-txns-rangeproof-too-large");
    }
    g_con_elementsmode = old_ct_mode;
}

// A2: range proof one byte over the max is rejected immediately before
// any balance verification.
BOOST_AUTO_TEST_CASE(rangeproof_over_max_rejected)
{
    const COutPoint prevout{TxidFromString("0x7777"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 1000 * COIN);
    mtx.nVersion = CT_VERSION;
    mtx.vout[0].vchRangeproof.assign(MAX_RANGEPROOF_SIZE + 1, 0x00);

    const CTransaction tx{mtx};
    TxValidationState state;
    CAmount txfee = -1;

    const bool old_ct_mode = g_con_elementsmode;
    g_con_elementsmode = true;
    BOOST_CHECK(!Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee));
    g_con_elementsmode = old_ct_mode;

    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-rangeproof-too-large");
    BOOST_CHECK(state.GetDebugMessage().find("rangeproof too large") != std::string::npos);
    BOOST_CHECK(state.GetDebugMessage().find("5135") != std::string::npos);
}

// RCPU hardening (C-1): an undefined transaction version (v4+) deserializes
// with the CT wire format (nVersion >= CT_VERSION) but is only validated for
// nVersion == CT_VERSION. Without the version whitelist it would fall through
// to the legacy accounting, ignoring commitment outputs: a v4 tx carrying a
// 500 RCPU commitment would effectively spend it from nothing (unlimited
// mint). Both the consensus entry CheckTransaction and the defense-in-depth
// CheckTxInputs must reject it.
BOOST_AUTO_TEST_CASE(v4_mint_rejected)
{
    const COutPoint prevout{TxidFromString("0x8888"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};

    // v4: 1 explicit input -> 1 explicit output + 1 commitment output.
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 500 * COIN);
    mtx.nVersion = CT_VERSION + 1;
    CTxOut comm_out;
    uint256 blind, nonce;
    BOOST_REQUIRE(BlindOutput(comm_out.nValue, comm_out.nNonce, comm_out.vchRangeproof, blind, nonce, 500 * COIN));
    comm_out.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(std::move(comm_out));

    const CTransaction tx{mtx};

    // Consensus entry point: `bad-txns-version`.
    TxValidationState state_tx;
    BOOST_CHECK(!CheckTransaction(tx, state_tx));
    BOOST_CHECK(state_tx.IsInvalid());
    BOOST_CHECK_EQUAL(state_tx.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state_tx.GetRejectReason(), "bad-txns-version");

    // Defense in depth: CheckTxInputs also rejects the undefined version.
    TxValidationState state_inputs;
    CAmount txfee = -1;
    BOOST_CHECK(!Consensus::CheckTxInputs(tx, state_inputs, view, /*nSpendHeight=*/200, txfee));
    BOOST_CHECK(state_inputs.IsInvalid());
    BOOST_CHECK_EQUAL(state_inputs.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state_inputs.GetRejectReason(), "bad-txns-version");
}

// RCPU hardening (C-1): a legacy (non-CT) transaction carrying a commitment
// output must be rejected at the consensus entry point. Even if the version
// whitelist were ever loosened, this guarantees a non-CT tx can never smuggle
// a commitment into the UTXO set.
BOOST_AUTO_TEST_CASE(nonct_commitment_output_rejected)
{
    const COutPoint prevout{TxidFromString("0x9999"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};

    // v2 with an explicit output + a commitment output.
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 500 * COIN);
    CTxOut comm_out;
    uint256 blind, nonce;
    BOOST_REQUIRE(BlindOutput(comm_out.nValue, comm_out.nNonce, comm_out.vchRangeproof, blind, nonce, 500 * COIN));
    comm_out.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(std::move(comm_out));
    BOOST_CHECK_EQUAL(mtx.nVersion, 2);

    const CTransaction tx{mtx};

    TxValidationState state;
    BOOST_CHECK(!CheckTransaction(tx, state));
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonct-commitment");

    // Defense in depth: CheckTxInputs rejects it too (B2 input rule fires on
    // the explicit input, but the version whitelist already ran, so this must
    // still be a consensus rejection, not a crash on GetAmount()).
    TxValidationState state_inputs;
    CAmount txfee = -1;
    BOOST_CHECK(!Consensus::CheckTxInputs(tx, state_inputs, view, /*nSpendHeight=*/200, txfee));
    BOOST_CHECK(state_inputs.IsInvalid());
}

// Regression: the C-1 hardening must not disturb legal v3 CT transactions.
// A v3 tx spending an explicit input to an explicit output plus an explicit
// fee output still passes VerifyAmounts (tally balance).
BOOST_AUTO_TEST_CASE(v3_explicit_spend_passes_verifyamounts)
{
    const COutPoint prevout{TxidFromString("0xaaaa"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 600 * COIN);
    mtx.nVersion = CT_VERSION;
    // Second output: fee-only output (explicit value, empty scriptPubKey).
    CTxOut fee_out;
    fee_out.nValue.SetToAmount(400 * COIN);
    mtx.vout.push_back(std::move(fee_out));

    const CTransaction tx{mtx};
    TxValidationState state;
    CAmount txfee = -1;

    const bool old_ct_mode = g_con_elementsmode;
    g_con_elementsmode = true;
    BOOST_CHECK(Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee));
    g_con_elementsmode = old_ct_mode;

    BOOST_CHECK(state.IsValid());
    BOOST_CHECK_EQUAL(txfee, 400 * COIN);
}

// RCPU hardening (P1-2): from nBanPathAHeight onward the legacy path-A
// plaintext-nonce encoding (33-byte CConfidentialNonce with 0x02 prefix,
// leaking the rewind nonce on-chain) must be rejected at the consensus
// layer with `bad-ct-legacy-nonce`. Below the activation height the same
// output remains legal (soft-fork, does not burn existing UTXOs).
BOOST_AUTO_TEST_CASE(path_a_plaintext_nonce_banned_from_height)
{
    const COutPoint prevout{TxidFromString("0xcccc"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};

    // v3 CT tx: 1 explicit input -> 1 explicit output + 1 fee output.
    // The first output carries the legacy path-A nonce encoding.
    CMutableTransaction mtx = MakeLegacySpend({prevout}, 600 * COIN);
    mtx.nVersion = CT_VERSION;
    mtx.vout[0].nNonce.vchCommitment.assign(33, 0x00);
    mtx.vout[0].nNonce.vchCommitment[0] = 0x02;
    CTxOut fee_out;
    fee_out.nValue.SetToAmount(400 * COIN);
    mtx.vout.push_back(std::move(fee_out));

    const CTransaction tx{mtx};
    const bool old_ct_mode = g_con_elementsmode;
    g_con_elementsmode = true;

    // Below activation height: legal.
    TxValidationState state_below;
    CAmount txfee = -1;
    BOOST_CHECK(Consensus::CheckTxInputs(tx, state_below, view, /*nSpendHeight=*/199, txfee,
        /*nCTActivationHeight=*/0, /*nBanPathAHeight=*/200));
    BOOST_CHECK(state_below.IsValid());

    // At/above activation height: rejected as bad-ct-legacy-nonce.
    TxValidationState state_banned;
    CAmount txfee2 = -1;
    BOOST_CHECK(!Consensus::CheckTxInputs(tx, state_banned, view, /*nSpendHeight=*/200, txfee2,
        /*nCTActivationHeight=*/0, /*nBanPathAHeight=*/200));
    BOOST_CHECK(state_banned.IsInvalid());
    BOOST_CHECK_EQUAL(state_banned.GetResult(), TxValidationResult::TX_CONSENSUS);
    BOOST_CHECK_EQUAL(state_banned.GetRejectReason(), "bad-ct-legacy-nonce");

    g_con_elementsmode = old_ct_mode;
}

// RCPU hardening (P1-2): a legal path-B nonce (33-byte 0x03 prefix, ECDH
// ephemeral pubkey) must NOT be rejected even above nBanPathAHeight. Only
// the 0x02 plaintext-nonce prefix is banned.
BOOST_AUTO_TEST_CASE(path_b_nonce_allowed_after_height)
{
    const COutPoint prevout{TxidFromString("0xdddd"), 0};

    CCoinsViewTest base;
    base.Add(prevout, MakeExplicitCoin(1000 * COIN));

    CCoinsViewCache view{&base};

    CMutableTransaction mtx = MakeLegacySpend({prevout}, 600 * COIN);
    mtx.nVersion = CT_VERSION;
    // Path B: 33-byte encoding with 0x03 prefix (odd Y pubkey).
    mtx.vout[0].nNonce.vchCommitment.assign(33, 0x00);
    mtx.vout[0].nNonce.vchCommitment[0] = 0x03;
    mtx.vout[0].nNonce.vchCommitment[1] = 0x01; // keep bytes non-trivial
    CTxOut fee_out;
    fee_out.nValue.SetToAmount(400 * COIN);
    mtx.vout.push_back(std::move(fee_out));

    const CTransaction tx{mtx};
    const bool old_ct_mode = g_con_elementsmode;
    g_con_elementsmode = true;

    TxValidationState state;
    CAmount txfee = -1;
    BOOST_CHECK(Consensus::CheckTxInputs(tx, state, view, /*nSpendHeight=*/200, txfee,
        /*nCTActivationHeight=*/0, /*nBanPathAHeight=*/0));
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK_NE(state.GetRejectReason(), "bad-ct-legacy-nonce");

    g_con_elementsmode = old_ct_mode;
}

BOOST_AUTO_TEST_SUITE_END()