// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <blind.h>
#include <confidential_validation.h>
#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(confidential_validation_tests, BasicTestingSetup)

// An all-explicit, fully balanced transaction validates and reports a zero fee.
BOOST_AUTO_TEST_CASE(explicit_balance_zero_fee)
{
    const CAmount amount = 500000;

    CTxOut input;
    input.nValue = amount;
    input.scriptPubKey = CScript() << OP_TRUE;

    CMutableTransaction mtx;
    mtx.nVersion = 1;
    // Use a non-null prevout so this is not treated as a coinbase.
    mtx.vin.emplace_back(CTxIn(COutPoint(TxidFromString("0x1111"), 0), CScript(), 0xffffffff));
    CTxOut out;
    out.nValue = amount;
    out.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(out);

    const CTransaction tx(mtx);
    const std::vector<CTxOut> inputs{input};

    CAmount fee = -1;
    BOOST_CHECK(VerifyAmounts(inputs, tx, fee));
    BOOST_CHECK_EQUAL(fee, CAmount(0));
}

// An explicit transaction whose outputs exceed its inputs is inflationary and
// must be rejected by the commitment balance check.
BOOST_AUTO_TEST_CASE(explicit_inflation_rejected)
{
    CTxOut input;
    input.nValue = 1000;
    input.scriptPubKey = CScript() << OP_TRUE;

    CMutableTransaction mtx;
    mtx.nVersion = 1;
    // Non-null prevout keeps this from being classified as a coinbase.
    mtx.vin.emplace_back(CTxIn(COutPoint(TxidFromString("0x2222"), 0), CScript(), 0xffffffff));
    CTxOut out;
    out.nValue = 1001; // more than the input
    out.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(out);

    const CTransaction tx(mtx);
    const std::vector<CTxOut> inputs{input};

    CAmount fee = -1;
    BOOST_CHECK(!VerifyAmounts(inputs, tx, fee));
}

// A v3 transaction carrying a committed (confidential) output with an empty
// range proof must be rejected: every confidential output needs a valid proof
// bounding its value to [0, 2^64), otherwise a miner could inflate supply.
BOOST_AUTO_TEST_CASE(committed_output_empty_rangeproof_rejected)
{
    CTxOut input;
    input.nValue = 1000 * COIN;
    input.scriptPubKey = CScript() << OP_TRUE;

    CMutableTransaction mtx;
    mtx.nVersion = CT_VERSION;
    mtx.vin.emplace_back(CTxIn(COutPoint(TxidFromString("0x3333"), 0), CScript(), 0xffffffff));

    // Build a structurally valid confidential output: a real Pedersen
    // commitment (so commitment parse succeeds) but with the range proof
    // stripped. The rejection must come from the range-proof check itself,
    // not from an unparsable commitment.
    CTxOut conf;
    uint256 blind, nonce;
    BOOST_REQUIRE(BlindOutput(conf.nValue, conf.nNonce, conf.vchRangeproof, blind, nonce, 1));
    conf.vchRangeproof.clear(); // no range proof
    conf.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(conf);

    CTxOut fee;
    fee.nValue.SetToAmount(1); // explicit fee output is required for CT txs
    fee.scriptPubKey.clear();  // IsFee(): explicit + empty script
    mtx.vout.push_back(fee);

    const CTransaction tx(mtx);
    const std::vector<CTxOut> inputs{input};

    CAmount txfee = 0;
    BOOST_CHECK(!VerifyAmounts(inputs, tx, txfee));
}

// A committed output whose range proof is garbage must also be rejected:
// same valid commitment, meaningless proof bytes instead.
BOOST_AUTO_TEST_CASE(committed_output_bad_rangeproof_rejected)
{
    CTxOut input;
    input.nValue = 1000 * COIN;
    input.scriptPubKey = CScript() << OP_TRUE;

    CMutableTransaction mtx;
    mtx.nVersion = CT_VERSION;
    mtx.vin.emplace_back(CTxIn(COutPoint(TxidFromString("0x4444"), 0), CScript(), 0xffffffff));

    CTxOut conf;
    uint256 blind, nonce;
    BOOST_REQUIRE(BlindOutput(conf.nValue, conf.nNonce, conf.vchRangeproof, blind, nonce, 1));
    conf.vchRangeproof.assign({0xde, 0xad, 0xbe, 0xef, 0x00, 0x01, 0x02, 0x03}); // garbage proof
    conf.scriptPubKey = CScript() << OP_TRUE;
    mtx.vout.push_back(conf);

    CTxOut fee;
    fee.nValue.SetToAmount(1);
    fee.scriptPubKey.clear();
    mtx.vout.push_back(fee);

    const CTransaction tx(mtx);
    const std::vector<CTxOut> inputs{input};

    CAmount txfee = 0;
    BOOST_CHECK(!VerifyAmounts(inputs, tx, txfee));
}

BOOST_AUTO_TEST_SUITE_END()