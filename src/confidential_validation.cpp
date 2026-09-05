// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <confidential_validation.h>

#include <consensus/amount.h>
#include <primitives/confidential.h>
#include <random.h>
#include <secp256k1.h>
#include <secp256k1_generator.h>
#include <secp256k1_rangeproof.h>

#include <cassert>
#include <cstring>

namespace {

/** Lazily-initialized secp256k1 context for CT consensus verification.
 *  SIGN is required because secp256k1_pedersen_commit multiplies by the
 *  base point G (uses the ecmult_gen context) even for zero blinding. */
secp256k1_context* GetCTContext()
{
    // Randomize the context right after creation so constant-time tables are
    // seeded with process-local randomness, mitigating timing/power side
    // channels during range-proof and Pedersen commitment verification.
    static secp256k1_context* ctx = []() {
        secp256k1_context* c = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        assert(c != nullptr);
        unsigned char seed[32];
        GetStrongRandBytes(seed);
        secp256k1_context_randomize(c, seed);
        return c;
    }();
    return ctx;
}

/** Build a Pedersen commitment to an explicit value (zero blinding factor):
 *  C = 0*G + value*H. */
bool ExplicitCommit(secp256k1_pedersen_commitment& commit, CAmount value)
{
    unsigned char blind[32];
    std::memset(blind, 0, sizeof(blind));
    return secp256k1_pedersen_commit(GetCTContext(), &commit, blind, static_cast<uint64_t>(value), secp256k1_generator_h) == 1;
}

/** Verify the range proof for a confidential (committed) output. */
bool VerifyRangeProof(const CTxOut& out)
{
    const CConfidentialValue& val = out.nValue;
    if (!val.IsCommitment()) {
        // Explicit values need no range proof.
        return true;
    }
    if (out.vchRangeproof.empty()) {
        return false;
    }
    secp256k1_pedersen_commitment commit;
    if (secp256k1_pedersen_commitment_parse(GetCTContext(), &commit, val.vchCommitment.data()) != 1) {
        return false;
    }
    uint64_t min_value = 0, max_value = 0;
    return secp256k1_rangeproof_verify(GetCTContext(), &min_value, &max_value, &commit,
                                       out.vchRangeproof.data(), out.vchRangeproof.size(),
                                       nullptr, 0, secp256k1_generator_h) == 1;
}

} // namespace

bool VerifyAmounts(const std::vector<CTxOut>& inputs, const CTransaction& tx, CAmount& txfee_out)
{
    assert(!tx.IsCoinBase());
    assert(inputs.size() == tx.vin.size());

    secp256k1_context* ctx = GetCTContext();

    std::vector<secp256k1_pedersen_commitment> vDataIn(inputs.size());
    std::vector<secp256k1_pedersen_commitment> vDataOut(tx.vout.size());
    std::vector<secp256k1_pedersen_commitment*> vpIn, vpOut;
    vpIn.reserve(inputs.size());
    vpOut.reserve(tx.vout.size() + 1);

    // Tally input commitments.
    for (size_t i = 0; i < inputs.size(); ++i) {
        const CConfidentialValue& val = inputs[i].nValue;
        if (val.IsNull()) {
            return false;
        }
        if (val.IsExplicit()) {
            if (!MoneyRange(val.GetAmount())) {
                return false;
            }
            if (!ExplicitCommit(vDataIn[i], val.GetAmount())) {
                return false;
            }
        } else if (val.IsCommitment()) {
            if (secp256k1_pedersen_commitment_parse(ctx, &vDataIn[i], val.vchCommitment.data()) != 1) {
                return false;
            }
        } else {
            return false;
        }
        vpIn.push_back(&vDataIn[i]);
    }

    // Tally output commitments, verify range proofs, and validate fee outputs.
    CAmount total_fee = 0;
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        const CTxOut& out = tx.vout[i];
        const CConfidentialValue& val = out.nValue;
        if (out.IsFee()) {
            // Fee output: must be explicit, non-zero, and within range.
            if (!val.IsExplicit() || val.GetAmount() <= 0 || !MoneyRange(val.GetAmount())) {
                return false;
            }
            total_fee += val.GetAmount();
            if (!MoneyRange(total_fee)) {
                return false;
            }
        }
        if (val.IsNull()) {
            return false;
        }
        if (val.IsExplicit()) {
            if (!MoneyRange(val.GetAmount())) {
                return false;
            }
            if (!ExplicitCommit(vDataOut[i], val.GetAmount())) {
                return false;
            }
        } else if (val.IsCommitment()) {
            if (secp256k1_pedersen_commitment_parse(ctx, &vDataOut[i], val.vchCommitment.data()) != 1) {
                return false;
            }
            if (!VerifyRangeProof(out)) {
                return false;
            }
        } else {
            return false;
        }
        vpOut.push_back(&vDataOut[i]);
    }

    // RCPU: if any output is confidential, there must be at least one
    // explicit fee output so miners can identify and collect the fee.
    // All-explicit v3 transactions are allowed with zero fee (same as v2),
    // but are discouraged via IsStandardTx policy.
    bool has_confidential_output = false;
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        if (tx.vout[i].nValue.IsCommitment()) {
            has_confidential_output = true;
            break;
        }
    }
    if (has_confidential_output && total_fee <= 0) {
        return false;
    }

    // Balance: sum(inputs) - sum(outputs) == 0 (the explicit fee output is part
    // of the outputs and carries the fee; blinded change balances the blinds).
    if (!secp256k1_pedersen_verify_tally(ctx, vpIn.data(), vpIn.size(), vpOut.data(), vpOut.size())) {
        return false;
    }

    txfee_out = total_fee;
    return true;
}
