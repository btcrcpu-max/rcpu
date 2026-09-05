// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLIND_H
#define BITCOIN_BLIND_H

#include <consensus/amount.h>
#include <primitives/confidential.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <vector>

class CKey;
class CPubKey;

/**
 * Blind a single output: generate a blinding factor and a nonce, create the
 * Pedersen value commitment, store the nonce commitment, and produce the
 * range proof. The output amount remains recoverable by the holder of the
 * nonce via UnblindValue().
 */
bool BlindOutput(CConfidentialValue& conf_value, CConfidentialNonce& nonce_commit,
                 std::vector<unsigned char>& rangeproof, uint256& blind, uint256& nonce,
                 CAmount amount);

/**
 * Unblind a confidential output via range proof rewind, recovering the
 * committed amount and the blinding factor using the nonce commitment.
 */
bool UnblindValue(const CConfidentialValue& conf_value, const CConfidentialNonce& nonce_commit,
                  const std::vector<unsigned char>& rangeproof, CAmount& amount_out, uint256& blind_out);

/** Get the amount of a txout, unblinding confidential outputs via range-proof
 * rewind. Returns 0 if the output is confidential and unblinding fails. */
CAmount GetOutputAmount(const CTxOut& txout);

/**
 * Blind every output of a transaction (single-asset).
 *
 * The output blinding factors are chosen so that sum(input_blinds) ==
 * sum(output_blinds) — this is the blinding-balance requirement of CT.
 * @param input_blinds blinding factor of each input (zero for explicit inputs)
 * @param[in,out] tx   transaction whose outputs are to be blinded (outputs must
 *                     currently carry explicit amounts)
 * @param[out] output_blinds the resulting blinding factor of each output
 * @param[out] output_nonces the nonce of each output
 * @return true on success
 */
bool BlindTransaction(const std::vector<uint256>& input_blinds, CMutableTransaction& tx,
                      std::vector<uint256>& output_blinds, std::vector<uint256>& output_nonces);

/**
 * Blind an output to a specific recipient using ECDH. The nonce commitment
 * carries the ephemeral public key, so the recipient can unblind using only
 * their private key (via UnblindValueWithKey) — no out-of-band nonce needed.
 */
bool BlindOutputToRecipient(CConfidentialValue& conf_value, CConfidentialNonce& nonce_commit,
                            std::vector<unsigned char>& rangeproof, uint256& blind,
                            CAmount amount, const CPubKey& recipient_pubkey);

/**
 * Unblind a confidential output using the recipient's private key (ECDH).
 */
bool UnblindValueWithKey(const CKey& blinding_key, const CConfidentialValue& conf_value,
                         const CConfidentialNonce& nonce_commit, const std::vector<unsigned char>& rangeproof,
                         CAmount& amount_out, uint256& blind_out);

#endif // BITCOIN_BLIND_H
