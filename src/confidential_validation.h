// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONFIDENTIAL_VALIDATION_H
#define BITCOIN_CONFIDENTIAL_VALIDATION_H

#include <consensus/amount.h>
#include <primitives/transaction.h>

#include <vector>

/**
 * Verify a transaction's confidential value balance and range proofs.
 *
 * Checks (in CT mode only) that:
 *   1. every confidential (committed) output has a valid range proof, and
 *   2. the Pedersen commitment tally balances:
 *        sum(input commitments) = sum(output commitments) + fee*H
 *      where fee = explicit_input_sum - explicit_output_sum (>= 0).
 *
 * This enforces the no-inflation and no-negative-value guarantees of
 * Confidential Transactions.
 *
 * @param inputs the previous outputs being spent (size == tx.vin.size())
 * @param tx     the transaction being validated (not a coinbase)
 * @param[out] txfee_out the explicit fee (explicit_in_sum - explicit_out_sum)
 * @return true if the value balance and all range proofs are valid
 */
bool VerifyAmounts(const std::vector<CTxOut>& inputs, const CTransaction& tx, CAmount& txfee_out);

#endif // BITCOIN_CONFIDENTIAL_VALIDATION_H
