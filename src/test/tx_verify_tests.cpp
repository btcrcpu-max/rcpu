// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <blind.h>
#include <coins.h>
#include <consensus/amount.h>
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

BOOST_AUTO_TEST_SUITE_END()