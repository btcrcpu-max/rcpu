// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blind.h>

#include <key.h>
#include <pubkey.h>
#include <random.h>
#include <span.h>
#include <secp256k1.h>
#include <secp256k1_ecdh.h>
#include <secp256k1_generator.h>
#include <secp256k1_rangeproof.h>

#include <cassert>
#include <cstring>
#include <optional>

// !RCPU FIX H-1: secp256k1_ecdh hash callback. Copies x32 directly to output
// (no SHA256) to match the original nonce derivation for backward compatibility.
// The constant-time guarantee comes from the secp256k1_ecdh API itself.
static int CopyX32(unsigned char* output, const unsigned char* x32, const unsigned char* y32, void* data)
{
    std::memcpy(output, x32, 32);
    return 1;
}

namespace {

secp256k1_context* GetBlindContext()
{
    // Randomize the context right after creation so the ecmult_gen precomputation
    // table is seeded with process-local randomness. This mitigates timing/power
    // side-channel attacks on the blind-factor operations behind Pedersen commitments.
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

// Generate 32 uniformly-random (cryptographically strong) bytes.
static void Rand32(uint256& out)
{
    unsigned char b[32];
    GetStrongRandBytes(b);
    std::memcpy(out.begin(), b, 32);
}

// Encode a 32-byte nonce into a 33-byte CConfidentialNonce (0x02 prefix + nonce bytes).
void SetNonce(CConfidentialNonce& nc, const uint256& nonce)
{
    nc.vchCommitment.resize(33);
    nc.vchCommitment[0] = 0x02;
    std::memcpy(&nc.vchCommitment[1], nonce.begin(), 32);
}

// Path A commits to the raw nonce with a fixed 0x02 prefix (BlindOutput /
// SetNonce). Any other encoding — including the ECDH path B carried in a
// 33-byte ephemeral pubkey (0x02/0x03) — must not be treated as a plaintext
// nonce: GetNonce() would silently decode garbage into the rangeproof rewind.
static bool IsLegacyNonceCommit(const CConfidentialNonce& nc)
{
    return nc.vchCommitment.size() == 33 && nc.vchCommitment[0] == 0x02;
}

static bool ComputeECDHNonce(const CKey& privkey, const CPubKey& pubkey, uint256& nonce_out)
{
    secp256k1_context* ctx = GetBlindContext();
    secp256k1_pubkey sp;
    if (!secp256k1_ec_pubkey_parse(ctx, &sp, pubkey.data(), pubkey.size())) {
        return false;
    }
    if (secp256k1_ecdh(ctx, nonce_out.begin(), &sp, UCharCast(privkey.begin()), CopyX32, nullptr) != 1) {
        return false;
    }
    return true;
}

} // namespace

// Decode a 33-byte CConfidentialNonce back to a 32-byte nonce. Only valid for
// path A (0x02 prefix + nonce); malformed commitments yield the zero nonce so
// callers must gate on IsLegacyNonceCommit before deriving anything from the
// result.
uint256 GetNonce(const CConfidentialNonce& nc)
{
    uint256 nonce;
    if (!IsLegacyNonceCommit(nc)) {
        return uint256();
    }
    std::memcpy(nonce.begin(), &nc.vchCommitment[1], 32);
    return nonce;
}

bool BlindOutput(CConfidentialValue& conf_value, CConfidentialNonce& nonce_commit,
                 std::vector<unsigned char>& rangeproof, uint256& blind, uint256& nonce,
                 CAmount amount)
{
    if (amount < 0 || !MoneyRange(amount)) {
        return false;
    }
    secp256k1_context* ctx = GetBlindContext();

    Rand32(blind);
    Rand32(nonce);

    // Commitment: C = amount*H + blind*G
    secp256k1_pedersen_commitment commit;
    if (secp256k1_pedersen_commit(ctx, &commit, blind.begin(), static_cast<uint64_t>(amount), secp256k1_generator_h) != 1) {
        return false; // blind out of range (astronomically rare)
    }

    unsigned char ser[33];
    secp256k1_pedersen_commitment_serialize(ctx, ser, &commit);
    conf_value.vchCommitment.assign(ser, ser + 33);

    SetNonce(nonce_commit, nonce);

    unsigned char proof[5134];
    size_t plen = sizeof(proof);
    if (secp256k1_rangeproof_sign(ctx, proof, &plen, 0, &commit, blind.begin(), nonce.begin(),
                                  0, 0, static_cast<uint64_t>(amount), nullptr, 0, nullptr, 0,
                                  secp256k1_generator_h) != 1) {
        return false;
    }
    rangeproof.assign(proof, proof + plen);
    return true;
}

bool UnblindValue(const CConfidentialValue& conf_value, const CConfidentialNonce& nonce_commit,
                  const std::vector<unsigned char>& rangeproof, CAmount& amount_out, uint256& blind_out)
{
    // Path A only: the nonce commitment must be a well-formed legacy nonce
    // (0x02 prefix + 32 bytes). A malformed commitment must never silently
    // rewind into a garbage amount.
    if (!conf_value.IsCommitment() || !IsLegacyNonceCommit(nonce_commit) || rangeproof.empty()) {
        return false;
    }
    secp256k1_context* ctx = GetBlindContext();

    secp256k1_pedersen_commitment commit;
    if (secp256k1_pedersen_commitment_parse(ctx, &commit, conf_value.vchCommitment.data()) != 1) {
        return false;
    }

    uint256 nonce = GetNonce(nonce_commit);
    uint64_t value = 0;
    uint64_t min_value = 0, max_value = 0;
    if (secp256k1_rangeproof_rewind(ctx, blind_out.begin(), &value, nullptr, nullptr, nonce.begin(),
                                    &min_value, &max_value, &commit, rangeproof.data(), rangeproof.size(),
                                    nullptr, 0, secp256k1_generator_h) != 1) {
        return false;
    }
    amount_out = static_cast<CAmount>(value);
    return true;
}

std::optional<CAmount> GetOutputAmount(const CTxOut& txout)
{
    if (txout.nValue.IsExplicit()) {
        return txout.nValue.GetAmount();
    }
    CAmount amount = 0;
    uint256 blind;
    if (UnblindValue(txout.nValue, txout.nNonce, txout.vchRangeproof, amount, blind)) {
        return amount;
    }
    // Unblind failure must not be conflated with a genuine zero amount: the
    // commitment may be malformed (bad nonce prefix / length / empty proof).
    return std::nullopt;
}

bool BlindTransaction(const std::vector<uint256>& input_blinds, CMutableTransaction& tx,
                      std::vector<uint256>& output_blinds, std::vector<uint256>& output_nonces,
                      const std::vector<std::optional<CPubKey>>& recipient_keys)
{
    secp256k1_context* ctx = GetBlindContext();
    const size_t n = tx.vout.size();
    if (n == 0) {
        return false;
    }
    if (!recipient_keys.empty() && recipient_keys.size() != n) {
        // Per-output recipient key list must line up with the outputs.
        return false;
    }
    output_blinds.resize(n);
    output_nonces.resize(n);

// Balance against the last non-fee output; fees never take part.
    size_t last_ct = n;
    for (size_t i = 0; i < n; ++i) {
        if (!tx.vout[i].IsFee()) last_ct = i;
    }
    if (last_ct == n) {
        // Fee-only: nothing to commit, do not call pedersen_blind_sum.
        return true;
    }

    // Random blinds for all but the balancing output.
    for (size_t i = 0; i < last_ct; ++i) {
        if (tx.vout[i].IsFee()) continue;
        Rand32(output_blinds[i]);
    }

    std::vector<const unsigned char*> blinds;
    blinds.reserve(input_blinds.size() + last_ct);
    for (const uint256& b : input_blinds) {
        blinds.push_back(b.begin());
    }
    for (size_t i = 0; i < last_ct; ++i) {
        if (tx.vout[i].IsFee()) continue;
        blinds.push_back(output_blinds[i].begin());
    }
    if (blinds.empty()) {
        // Single CT output with no input blinds: it balances itself.
        Rand32(output_blinds[last_ct]);
    } else if (!secp256k1_pedersen_blind_sum(ctx, output_blinds[last_ct].begin(),
                                             blinds.data(), blinds.size(),
                                             input_blinds.size())) {
        return false;
    }

    // Blind each output.
    for (size_t i = 0; i < n; ++i) {
        if (tx.vout[i].IsFee()) continue;
        const CAmount amount = tx.vout[i].nValue.GetAmount();
        if (amount < 0 || !MoneyRange(amount)) {
            return false;
        }

        const bool path_b = !recipient_keys.empty() && recipient_keys[i].has_value();
        uint256 nonce;
        if (path_b) {
            // Path B (recipient ECDH): the nonce commitment carries the
            // ephemeral public key; the recipient derives the nonce from
            // their own private key (UnblindValueWithKey) — no out-of-band
            // nonce required. Non-fee outputs without an engaged key fail
            // closed: an external output must not silently fall back to the
            // plaintext-nonce path A.
            CKey ephemeral;
            CPubKey ephemeral_pub;
            // Force an odd-Y compressed pubkey (0x03 prefix): a 0x02 prefix
            // would be misdetected as a legacy plaintext-nonce commitment by
            // IsLegacyNonceCommit, letting a third party feed the pubkey X
            // bytes into GetNonce() and attempt a garbage rewind. With a
            // 0x03 prefix the commitment is never legacy-shaped.
            do {
                ephemeral.MakeNewKey(true);
                ephemeral_pub = ephemeral.GetPubKey();
            } while (ephemeral_pub.size() != 33 || ephemeral_pub.data()[0] == 0x02);
            tx.vout[i].nNonce.vchCommitment.assign(ephemeral_pub.begin(), ephemeral_pub.end());

            if (!ComputeECDHNonce(ephemeral, *recipient_keys[i], nonce)) {
                return false;
            }
            output_nonces[i] = uint256(); // ECDH-derived; not a stored nonce
        } else {
            if (!recipient_keys.empty()) {
                // Engaged key list but this output has none: reject rather
                // than blinding an external recipient with the legacy path.
                return false;
            }
            Rand32(output_nonces[i]);
            SetNonce(tx.vout[i].nNonce, output_nonces[i]);
            nonce = output_nonces[i];
        }

        secp256k1_pedersen_commitment commit;
        if (secp256k1_pedersen_commit(ctx, &commit, output_blinds[i].begin(), static_cast<uint64_t>(amount), secp256k1_generator_h) != 1) {
            return false;
        }

        unsigned char ser[33];
        secp256k1_pedersen_commitment_serialize(ctx, ser, &commit);
        tx.vout[i].nValue.vchCommitment.assign(ser, ser + 33);

        unsigned char proof[5134];
        size_t plen = sizeof(proof);
        if (secp256k1_rangeproof_sign(ctx, proof, &plen, 0, &commit, output_blinds[i].begin(), nonce.begin(),
                                      0, 0, static_cast<uint64_t>(amount), nullptr, 0, nullptr, 0,
                                      secp256k1_generator_h) != 1) {
            return false;
        }
        tx.vout[i].vchRangeproof.assign(proof, proof + plen);
    }

    return true;
}

bool BlindOutputToRecipient(CConfidentialValue& conf_value, CConfidentialNonce& nonce_commit,
                            std::vector<unsigned char>& rangeproof, uint256& blind,
                            CAmount amount, const CPubKey& recipient_pubkey)
{
    if (amount < 0 || !MoneyRange(amount) || !recipient_pubkey.IsValid()) {
        return false;
    }
    secp256k1_context* ctx = GetBlindContext();

    // Ephemeral keypair for ECDH. Force an odd-Y compressed pubkey (0x03
    // prefix): a 0x02 prefix would be misdetected as a legacy path-A
    // plaintext-nonce commitment by IsLegacyNonceCommit, and is rejected by
    // consensus from nBanPathAHeight (bad-ct-legacy-nonce) -- ~50% of outputs
    // would be either garbled on unblind or refused on-chain.
    CKey ephemeral;
    CPubKey ephemeral_pub;
    do {
        ephemeral.MakeNewKey(true);
        ephemeral_pub = ephemeral.GetPubKey();
    } while (ephemeral_pub.size() != 33 || ephemeral_pub.data()[0] == 0x02);

    // The nonce commitment carries the ephemeral public key.
    nonce_commit.vchCommitment.assign(ephemeral_pub.begin(), ephemeral_pub.end());

    uint256 nonce;
    if (!ComputeECDHNonce(ephemeral, recipient_pubkey, nonce)) {
        return false;
    }

    Rand32(blind);

    secp256k1_pedersen_commitment commit;
    if (secp256k1_pedersen_commit(ctx, &commit, blind.begin(), static_cast<uint64_t>(amount), secp256k1_generator_h) != 1) {
        return false;
    }
    unsigned char ser[33];
    secp256k1_pedersen_commitment_serialize(ctx, ser, &commit);
    conf_value.vchCommitment.assign(ser, ser + 33);

    unsigned char proof[5134];
    size_t plen = sizeof(proof);
    if (secp256k1_rangeproof_sign(ctx, proof, &plen, 0, &commit, blind.begin(), nonce.begin(),
                                  0, 0, static_cast<uint64_t>(amount), nullptr, 0, nullptr, 0,
                                  secp256k1_generator_h) != 1) {
        return false;
    }
    rangeproof.assign(proof, proof + plen);
    return true;
}

bool UnblindValueWithKey(const CKey& blinding_key, const CConfidentialValue& conf_value,
                         const CConfidentialNonce& nonce_commit, const std::vector<unsigned char>& rangeproof,
                         CAmount& amount_out, uint256& blind_out)
{
    if (!conf_value.IsCommitment() || nonce_commit.vchCommitment.size() != 33 || rangeproof.empty()) {
        return false;
    }
    secp256k1_context* ctx = GetBlindContext();

    // The nonce commitment holds the ephemeral public key.
    secp256k1_pubkey ephemeral;
    if (!secp256k1_ec_pubkey_parse(ctx, &ephemeral, nonce_commit.vchCommitment.data(), 33)) {
        return false;
    }
    // !RCPU FIX H-1: Use secp256k1_ecdh for constant-time shared secret derivation.
    // The previous manual tweak_mul + serialize path bypassed the library's
    // constant-time guarantees. secp256k1_ecdh is the documented constant-time API.
    // CopyX32 preserves the original nonce derivation for backward compatibility.
    uint256 nonce;
    if (secp256k1_ecdh(ctx, nonce.begin(), &ephemeral, UCharCast(blinding_key.begin()), CopyX32, nullptr) != 1) {
        return false;
    }

    secp256k1_pedersen_commitment commit;
    if (secp256k1_pedersen_commitment_parse(ctx, &commit, conf_value.vchCommitment.data()) != 1) {
        return false;
    }
    uint64_t value = 0;
    uint64_t min_value = 0, max_value = 0;
    if (secp256k1_rangeproof_rewind(ctx, blind_out.begin(), &value, nullptr, nullptr, nonce.begin(),
                                    &min_value, &max_value, &commit, rangeproof.data(), rangeproof.size(),
                                    nullptr, 0, secp256k1_generator_h) != 1) {
        return false;
    }
    amount_out = static_cast<CAmount>(value);
    return true;
}
