// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

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

BOOST_AUTO_TEST_SUITE_END()