// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLIND_H
#define BITCOIN_BLIND_H

#include <consensus/amount.h>
#include <primitives/confidential.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <uint256.h>

#include <optional>
#include <vector>

class CKey;

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

/**
 * Decode a 33-byte legacy (path A) nonce commitment (0x02 prefix + 32 bytes)
 * back to the raw nonce. Malformed commitments (wrong length or prefix) yield
 * the zero nonce; callers must not treat that as a valid nonce and should
 * gate on the exact 33-byte / 0x02 encoding before use. Path B (recipient
 * ECDH) carries an ephemeral pubkey in the same 33 bytes and must go through
 * UnblindValueWithKey instead.
 */
/**
 * True if the nonce commitment is a legacy path-A plaintext nonce encoding
 * (33 bytes, 0x02 prefix + 32-byte nonce). Path B ECDH ephemeral pubkeys and
 * malformed commitments must not be treated as plaintext nonces.
 */
bool IsLegacyNonceCommit(const CConfidentialNonce& nc);

uint256 GetNonce(const CConfidentialNonce& nc);

/**
 * Get the amount of a txout, unblinding confidential outputs via range-proof
 * rewind. Returns nullopt if the output is confidential and unblinding fails
 * (malformed nonce commitment / empty or invalid range proof); failure must
 * not be conflated with a truthful zero amount.
 */
std::optional<CAmount> GetOutputAmount(const CTxOut& txout);

/**
 * Blind every output of a transaction (single-asset).
 *
 * The output blinding factors are chosen so that sum(input_blinds) ==
 * sum(output_blinds) — this is the blinding-balance requirement of CT.
 * @param input_blinds blinding factor of each input (zero for explicit inputs)
 * @param[in,out] tx   transaction whose outputs are to be blinded (outputs must
 *                     currently carry explicit amounts)
 * @param[out] output_blinds the resulting blinding factor of each output
 * @param[out] output_nonces the nonce of each output (path A) or the ephemeral
 *                           ECDH nonce (path B); path B nonces are also carried
 *                           implicitly by the recipient's key derivation
 * @param[in] recipient_keys optional per-output recipient public keys, indexed
 *                           by output position. When empty (or when every
 *                           entry is std::nullopt) the legacy path A is used
 *                           for all outputs. When an entry is engaged, that
 *                           output is blinded to the recipient via ECDH
 *                           (path B). The vector length must equal the number
 *                           of outputs, otherwise the call fails.
 * @param[in] explicit_outputs optional per-output keep-explicit marker, indexed
 *                           by output position. When non-null its length must
 *                           equal the number of outputs; an output marked true
 *                           is left unblinded (explicit value, no nonce, no
 *                           range proof) and takes no part in the blinding
 *                           balance. Used by the mainnet default send path to
 *                           emit plaintext (non-CT) outputs to recipients whose
 *                           public key is unknown instead of falling back to
 *                           the legacy plaintext-nonce path A.
 * @return true on success
 */
bool BlindTransaction(const std::vector<uint256>& input_blinds, CMutableTransaction& tx,
                      std::vector<uint256>& output_blinds, std::vector<uint256>& output_nonces,
                      const std::vector<std::optional<CPubKey>>& recipient_keys = {},
                      const std::vector<bool>* explicit_outputs = nullptr);

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
