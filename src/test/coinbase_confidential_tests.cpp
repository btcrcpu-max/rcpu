// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <core_io.h>
#include <primitives/block.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/data/cb_explicit_A.hex.h>
#include <test/data/cb_confidential_B.hex.h>
#include <test/data/cb_confidential_C.hex.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(coinbase_confidential_tests, BasicTestingSetup)

// Decode a full block (as captured during the P0 poison-block experiment)
// and return its coinbase transaction.
static const CTransaction& CoinbaseOfBlockHex(const std::string& hex)
{
    CBlock block;
    BOOST_REQUIRE(DecodeHexBlk(block, hex));
    BOOST_REQUIRE(!block.vtx.empty());
    BOOST_REQUIRE(block.vtx[0]->IsCoinBase());
    return *block.vtx[0];
}

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

// Real regtest poison blocks from the P0 experiment (see
// RCPU-P0-poison-verification-2026-09-12.md). These are full blocks, not
// hand-written transactions, so they exercise the exact serialization the
// network produced: an all-explicit coinbase must pass, while fully
// committed and mixed coinbases must both be rejected.
BOOST_AUTO_TEST_CASE(real_poison_block_explicit_A_passes)
{
    BlockValidationState state;
    BOOST_CHECK(CheckCoinbaseOutputsExplicit(
        CoinbaseOfBlockHex(hex_tests::cb_explicit_A), state));
}

BOOST_AUTO_TEST_CASE(real_poison_block_committed_B_rejected)
{
    BlockValidationState state;
    BOOST_CHECK(!CheckCoinbaseOutputsExplicit(
        CoinbaseOfBlockHex(hex_tests::cb_confidential_B), state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cb-confidential");
}

BOOST_AUTO_TEST_CASE(real_poison_block_mixed_C_rejected)
{
    BlockValidationState state;
    BOOST_CHECK(!CheckCoinbaseOutputsExplicit(
        CoinbaseOfBlockHex(hex_tests::cb_confidential_C), state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cb-confidential");
}

BOOST_AUTO_TEST_SUITE_END()
