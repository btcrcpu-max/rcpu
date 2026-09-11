// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(coinbase_confidential_tests, BasicTestingSetup)

// A coinbase whose outputs are all explicit passes the rule.
BOOST_AUTO_TEST_CASE(explicit_coinbase_passes)
{
    CMutableTransaction cb;
    cb.nVersion = 1;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << OP_0 << OP_0; // Any 2-byte scriptSig is fine for a coinbase.
    CTxOut out;
    out.nValue.SetToAmount(50 * COIN);
    out.scriptPubKey = CScript() << OP_TRUE;
    cb.vout.push_back(out);

    BlockValidationState state;
    BOOST_CHECK(CheckCoinbaseOutputsExplicit(CTransaction(cb), state));
}

// A coinbase whose output is a confidential (committed) value must be
// rejected: coinbase outputs skip VerifyAmounts and GetValueOut() counts
// commitments as 0, so this would otherwise mint coins out of thin air.
BOOST_AUTO_TEST_CASE(committed_coinbase_rejected)
{
    CMutableTransaction cb;
    cb.nVersion = CT_VERSION;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    CTxOut out;
    out.nValue.vchCommitment.assign(33, 0x08); // Commitments use prefix 0x08/0x09.
    out.scriptPubKey = CScript() << OP_TRUE;
    cb.vout.push_back(out);

    BlockValidationState state;
    BOOST_CHECK(!CheckCoinbaseOutputsExplicit(CTransaction(cb), state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cb-confidential");
}

// Multiple outputs: one explicit and one committed is still rejected (any
// single committed coinbase output is enough to break supply accounting).
BOOST_AUTO_TEST_CASE(mixed_coinbase_rejected)
{
    CMutableTransaction cb;
    cb.nVersion = CT_VERSION;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    CTxOut explicit_out;
    explicit_out.nValue.SetToAmount(1 * COIN);
    explicit_out.scriptPubKey = CScript() << OP_TRUE;
    cb.vout.push_back(explicit_out);
    CTxOut committed_out;
    committed_out.nValue.vchCommitment.assign(33, 0x09);
    committed_out.scriptPubKey = CScript() << OP_TRUE;
    cb.vout.push_back(committed_out);

    BlockValidationState state;
    BOOST_CHECK(!CheckCoinbaseOutputsExplicit(CTransaction(cb), state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cb-confidential");
}

BOOST_AUTO_TEST_SUITE_END()
