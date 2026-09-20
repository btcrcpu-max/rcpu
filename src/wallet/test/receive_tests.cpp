// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blind.h>
#include <key.h>
#include <primitives/confidential.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/solver.h>
#include <wallet/receive.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(receive_tests, WalletTestingSetup)

namespace {
//! Build a blinded output for `amount` payable to `recipient`. When path_b is
//! true the nonce commitment carries the ECDH ephemeral pubkey (0x03 prefix)
//! and only the recipient's private key can unblind it (UnblindValueWithKey);
//! otherwise the legacy path-A plaintext nonce (0x02 prefix) is used and the
//! range proof can be rewound without a key (UnblindValue).
CTxOut MakeBlindedOutput(CAmount amount, const CPubKey& recipient, bool path_b)
{
    CTxOut txout;
    uint256 blind;
    if (path_b) {
        BOOST_REQUIRE(BlindOutputToRecipient(txout.nValue, txout.nNonce, txout.vchRangeproof, blind, amount, recipient));
    } else {
        uint256 nonce;
        BOOST_REQUIRE(BlindOutput(txout.nValue, txout.nNonce, txout.vchRangeproof, blind, nonce, amount));
    }
    txout.scriptPubKey = GetScriptForRawPubKey(recipient);
    return txout;
}
} // namespace

//! N-01: a path-B (0x03 ECDH) output sent to one of the wallet's own public
//! keys must be unblinded by UnblindWalletOutput to the exact committed
//! amount, exposing the previously dead path-B unblinding of receive/coins
//! call sites.
BOOST_AUTO_TEST_CASE(unblind_wallet_output_path_b_owned)
{
    m_wallet.SetupLegacyScriptPubKeyMan();
    CKey own_key;
    own_key.MakeNewKey(true);
    const CPubKey own_pub = own_key.GetPubKey();
    {
        LOCK(m_wallet.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_REQUIRE(m_wallet.GetLegacyScriptPubKeyMan()->AddKey(own_key));
    }

    const CAmount amount = 123456789;
    const bool path_b = true;
    const CTxOut txout = MakeBlindedOutput(amount, own_pub, path_b);
    BOOST_CHECK_EQUAL(txout.nNonce.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(txout.nNonce.vchCommitment[0], 0x03);

    CAmount value = -1;
    uint256 blind;
    BOOST_REQUIRE(UnblindWalletOutput(m_wallet, txout, value, blind));
    BOOST_CHECK_EQUAL(value, amount);
    BOOST_CHECK(!blind.IsNull());
}

//! N-01: a path-B output bound to a foreign key (not in this wallet) must fail
//! closed -- UnblindWalletOutput returns false and must not fabricate a zero
//! amount, matching the caller-side "not ours" downgrade.
BOOST_AUTO_TEST_CASE(unblind_wallet_output_path_b_foreign)
{
    m_wallet.SetupLegacyScriptPubKeyMan();
    CKey foreign_key;
    foreign_key.MakeNewKey(true);

    const CAmount amount = 987654321;
    const bool path_b = true;
    const CTxOut txout = MakeBlindedOutput(amount, foreign_key.GetPubKey(), path_b);

    CAmount value = -1;
    uint256 blind;
    BOOST_CHECK(!UnblindWalletOutput(m_wallet, txout, value, blind));
    BOOST_CHECK_EQUAL(value, -1); // output untouched on failure
}

//! N-01: the legacy path-A (0x02 plaintext nonce) output keeps unblinding
//! through UnblindWalletOutput via range-proof rewind, with no key required.
BOOST_AUTO_TEST_CASE(unblind_wallet_output_path_a_rewind)
{
    m_wallet.SetupLegacyScriptPubKeyMan();
    CKey any_key;
    any_key.MakeNewKey(true);

    const CAmount amount = 424242;
    const bool path_b = false;
    const CTxOut txout = MakeBlindedOutput(amount, any_key.GetPubKey(), path_b);
    BOOST_CHECK_EQUAL(txout.nNonce.vchCommitment.size(), 33U);
    BOOST_CHECK_EQUAL(txout.nNonce.vchCommitment[0], 0x02);

    CAmount value = -1;
    uint256 blind;
    BOOST_REQUIRE(UnblindWalletOutput(m_wallet, txout, value, blind));
    BOOST_CHECK_EQUAL(value, amount);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet