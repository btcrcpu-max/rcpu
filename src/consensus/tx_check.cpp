// Copyright (c) 2017-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <tinyformat.h>
#include <util/string.h>

bool CheckTransaction(const CTransaction& tx, TxValidationState& state)
{
    // Basic checks that don't depend on any context

    // RCPU: consensus version whitelist. Serialization treats nVersion >=
    // CT_VERSION as the confidential (CT) wire format, but consensus
    // validation only handles nVersion == CT_VERSION (VerifyAmounts) and the
    // legacy plaintext versions. Without this whitelist, a v4+ transaction
    // would be deserialized as CT yet validated as legacy, allowing an
    // arbitrary-value commitment output to bypass VerifyAmounts and enter the
    // UTXO set (unlimited mint). Reject anything outside [1, CT_VERSION] at
    // the consensus layer; this is a tightening rule (soft-fork compatible:
    // no historical block carries nVersion > 3).
    if (tx.nVersion < 1 || tx.nVersion > CT_VERSION) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-version",
            strprintf("transaction version %d not allowed", tx.nVersion));
    }

    // RCPU: defense in depth. A legacy (non-CT) transaction must never carry
    // confidential outputs: commitment outputs would be invisible to the
    // legacy GetValueOut()/GetAmount() accounting and could never be spent by
    // a legacy tx. If CheckTxInputs is ever refactored, this guarantees a
    // non-CT tx cannot smuggle a commitment into the UTXO set.
    if (tx.nVersion != CT_VERSION) {
        for (const auto& txout : tx.vout) {
            if (txout.nValue.IsCommitment() || !txout.vchRangeproof.empty()) {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-nonct-commitment",
                    "non-CT transaction carries a confidential output");
            }
        }
    }

    if (tx.vin.empty())
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(TX_NO_WITNESS(tx)) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-oversize");
    }

    // Check for negative or overflow output values (see CVE-2010-5139)
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        // RCPU CT: confidential (committed) outputs are validated by VerifyAmounts,
        // which checks the Pedersen balance and the range proof.
        if (!txout.nValue.IsExplicit())
            continue;
        if (txout.nValue.GetAmount() < 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-negative");
        if (txout.nValue.GetAmount() > MAX_MONEY)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue.GetAmount();
        if (!MoneyRange(nValueOut))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs (see CVE-2018-17144)
    // While Consensus::CheckTxInputs does check if all inputs of a tx are available, and UpdateCoins marks all inputs
    // of a tx as spent, it does not check if the tx has duplicate inputs.
    // Failure to run this check will result in either a crash or an inflation bug, depending on the implementation of
    // the underlying coins database.
    std::set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputs-duplicate");
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-prevout-null");
    }

    return true;
}
