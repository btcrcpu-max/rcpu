// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blind.h>

#include <key.h>
#include <pubkey.h>
#include <random.h>
#include <span.h>
#include <secp256k1.h>
#include <secp256k1_generator.h>
#include <secp256k1_rangeproof.h>

#include <cassert>
#include <cstring>

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

// Decode a 33-byte CConfidentialNonce back to a 32-byte nonce.
uint256 GetNonce(const CConfidentialNonce& nc)
{
    uint256 nonce;
    std::memcpy(nonce.begin(), &nc.vchCommitment[1], 32);
    return nonce;
}

} // namespace

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
    if (!conf_value.IsCommitment() || nonce_commit.vchCommitment.size() != 33 || rangeproof.empty()) {
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

CAmount GetOutputAmount(const CTxOut& txout)
{
    if (txout.nValue.IsExplicit()) {
        return txout.nValue.GetAmount();
    }
    CAmount amount = 0;
    uint256 blind;
    if (UnblindValue(txout.nValue, txout.nNonce, txout.vchRangeproof, amount, blind)) {
        return amount;
    }
    return 0;
}

bool BlindTransaction(const std::vector<uint256>& input_blinds, CMutableTransaction& tx,
                      std::vector<uint256>& output_blinds, std::vector<uint256>& output_nonces)
{
    secp256k1_context* ctx = GetBlindContext();
    const size_t n = tx.vout.size();
    if (n == 0) {
        return false;
    }
    output_blinds.resize(n);
    output_nonces.resize(n);

    // Random blinds for all but the last output.
    for (size_t i = 0; i + 1 < n; ++i) {
        Rand32(output_blinds[i]);
        if (tx.vout[i].IsFee()) continue;
    }

    // Balance: last output blind = sum(inputs) - sum(other outputs).
    std::vector<const unsigned char*> blinds;
    for (const uint256& b : input_blinds) {
        blinds.push_back(b.begin());
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (tx.vout[i].IsFee()) continue;
        blinds.push_back(output_blinds[i].begin());
    }
    if (!secp256k1_pedersen_blind_sum(ctx, output_blinds[n - 1].begin(), blinds.data(), blinds.size(), input_blinds.size())) {
        return false;
    }

    // Blind each output.
    for (size_t i = 0; i < n; ++i) {
        if (tx.vout[i].IsFee()) continue;
        const CAmount amount = tx.vout[i].nValue.GetAmount();
        if (amount < 0 || !MoneyRange(amount)) {
            return false;
        }
        Rand32(output_nonces[i]);

        secp256k1_pedersen_commitment commit;
        if (secp256k1_pedersen_commit(ctx, &commit, output_blinds[i].begin(), static_cast<uint64_t>(amount), secp256k1_generator_h) != 1) {
            return false;
        }

        unsigned char ser[33];
        secp256k1_pedersen_commitment_serialize(ctx, ser, &commit);
        tx.vout[i].nValue.vchCommitment.assign(ser, ser + 33);

        SetNonce(tx.vout[i].nNonce, output_nonces[i]);

        unsigned char proof[5134];
        size_t plen = sizeof(proof);
        if (secp256k1_rangeproof_sign(ctx, proof, &plen, 0, &commit, output_blinds[i].begin(), output_nonces[i].begin(),
                                      0, 0, static_cast<uint64_t>(amount), nullptr, 0, nullptr, 0,
                                      secp256k1_generator_h) != 1) {
            return false;
        }
        tx.vout[i].vchRangeproof.assign(proof, proof + plen);
    }

    return true;
}

// Derive a 32-byte ECDH nonce: nonce = x-coordinate of (privkey * pubkey).
static bool ComputeECDHNonce(const CKey& privkey, const CPubKey& pubkey, uint256& nonce_out)
{
    secp256k1_context* ctx = GetBlindContext();
    secp256k1_pubkey sp;
    if (!secp256k1_ec_pubkey_parse(ctx, &sp, pubkey.data(), pubkey.size())) {
        return false;
    }
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &sp, UCharCast(privkey.begin()))) {
        return false;
    }
    unsigned char ser[33];
    size_t serlen = sizeof(ser);
    if (!secp256k1_ec_pubkey_serialize(ctx, ser, &serlen, &sp, SECP256K1_EC_COMPRESSED)) {
        return false;
    }
    std::memcpy(nonce_out.begin(), ser + 1, 32);
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

    // Ephemeral keypair for ECDH.
    CKey ephemeral;
    ephemeral.MakeNewKey(true);
    const CPubKey ephemeral_pub = ephemeral.GetPubKey();

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
    // shared = blinding_key * ephemeral_pubkey
    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &ephemeral, UCharCast(blinding_key.begin()))) {
        return false;
    }
    unsigned char ser[33];
    size_t serlen = sizeof(ser);
    if (!secp256k1_ec_pubkey_serialize(ctx, ser, &serlen, &ephemeral, SECP256K1_EC_COMPRESSED)) {
        return false;
    }
    uint256 nonce;
    std::memcpy(nonce.begin(), ser + 1, 32);

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
