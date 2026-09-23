// Copyright (c) 2014-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>

#include <base58.h>
#include <bech32.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <tinyformat.h>
#include <util/strencodings.h>

#include <algorithm>
#include <assert.h>
#include <string.h>

/// Maximum witness length for Bech32 addresses.
static constexpr std::size_t BECH32_WITNESS_PROG_MAX_LEN = 40;

/// RCPU CT confidential addresses encode the witness program together with
/// the compressed recipient public key (see key_io.h for the layout).
static constexpr unsigned char CONF_ADDR_VERSION_P2WPKH = 0x00;
static constexpr std::size_t CONF_ADDR_PROGRAM_LEN = 20;
static constexpr std::size_t CONF_ADDR_PUBKEY_LEN = CPubKey::COMPRESSED_SIZE;

static constexpr const char* CONF_BECH32M_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static std::vector<unsigned char> ConfBech32mHrpExpand(const std::string& hrp)
{
    std::vector<unsigned char> ret;
    for (char c : hrp) {
        ret.push_back(static_cast<unsigned char>(c) >> 5);
    }
    ret.push_back(0);
    for (char c : hrp) {
        ret.push_back(static_cast<unsigned char>(c) & 31);
    }
    return ret;
}

static uint32_t ConfBech32mPolymod(const std::vector<unsigned char>& values)
{
    static const uint32_t GEN[5] = {0x3b6a57b2u, 0x26508e6du, 0x1ea119fau, 0x3d4233ddu, 0x2a1462b3u};
    uint32_t chk = 1;
    for (unsigned char v : values) {
        const uint32_t b = chk >> 25;
        chk = ((chk & 0x1ffffffu) << 5) ^ v;
        for (int i = 0; i < 5; ++i) {
            if ((b >> i) & 1) chk ^= GEN[i];
        }
    }
    return chk;
}

/// Bech32m decoder that, unlike Bitcoin Core's bech32::Decode, accepts payloads
/// longer than 90 chars, which is required for 54-byte RCPU confidential
/// addresses (BIP-350 checksum constant 0x2bc830a3 is used).
static bool ConfBech32mDecode(std::string& hrp, std::vector<unsigned char>& data, const std::string& str)
{
    const size_t sep = str.rfind('1');
    if (sep == std::string::npos || sep < 1 || sep + 7 > str.size()) {
        return false;
    }
    hrp = str.substr(0, sep);
    for (char c : hrp) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 33 || uc > 126) return false;
    }
    data.clear();
    for (size_t i = sep + 1; i < str.size(); ++i) {
        const char* found = strchr(CONF_BECH32M_CHARSET, str[i]);
        if (found == nullptr) {
            return false;
        }
        data.push_back(static_cast<unsigned char>(found - CONF_BECH32M_CHARSET));
    }
    if (data.size() < 6) {
        return false;
    }
    std::vector<unsigned char> values = ConfBech32mHrpExpand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    if (ConfBech32mPolymod(values) != 0x2bc830a3u) {
        return false;
    }
    data.resize(data.size() - 6);
    return true;
}

namespace {
class DestinationEncoder
{
private:
    const CChainParams& m_params;

public:
    explicit DestinationEncoder(const CChainParams& params) : m_params(params) {}

    std::string operator()(const PKHash& id) const
    {
        // RCPU supports Base58 legacy (P2PKH) and P2SH-SegWit receive
        // addresses on every chain, including mainnet. The template prefix
        // bytes in chainparams are used for both encode and decode, so the
        // addresses round-trip correctly.
        std::vector<unsigned char> data = m_params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data);
    }

    std::string operator()(const ScriptHash& id) const
    {
        std::vector<unsigned char> data = m_params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data);
    }

    std::string operator()(const WitnessV0KeyHash& id) const
    {
        std::vector<unsigned char> data = {0};
        data.reserve(33);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
        return bech32::Encode(bech32::Encoding::BECH32, m_params.Bech32HRP(), data);
    }

    std::string operator()(const WitnessV0ScriptHash& id) const
    {
        std::vector<unsigned char> data = {0};
        data.reserve(53);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
        return bech32::Encode(bech32::Encoding::BECH32, m_params.Bech32HRP(), data);
    }

    std::string operator()(const WitnessV1Taproot& tap) const
    {
        std::vector<unsigned char> data = {1};
        data.reserve(53);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, tap.begin(), tap.end());
        return bech32::Encode(bech32::Encoding::BECH32M, m_params.Bech32HRP(), data);
    }

    std::string operator()(const WitnessUnknown& id) const
    {
        const std::vector<unsigned char>& program = id.GetWitnessProgram();
        if (id.GetWitnessVersion() < 1 || id.GetWitnessVersion() > 16 || program.size() < 2 || program.size() > 40) {
            return {};
        }
        std::vector<unsigned char> data = {(unsigned char)id.GetWitnessVersion()};
        data.reserve(1 + (program.size() * 8 + 4) / 5);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, program.begin(), program.end());
        return bech32::Encode(bech32::Encoding::BECH32M, m_params.Bech32HRP(), data);
    }

    std::string operator()(const CNoDestination& no) const { return {}; }
    std::string operator()(const PubKeyDestination& pk) const { return {}; }

    std::string operator()(const ConfidentialKeyHash& id) const
    {
        // RCPU CT: encode the spend program hash plus the embedded blinding
        // public key into a single confidential address ("rcpux1...").
        return EncodeConfidentialAddress(id.GetSpend(), id.GetBlinding(), m_params);
    }
};

CTxDestination DecodeDestination(const std::string& str, const CChainParams& params, std::string& error_str, std::vector<int>* error_locations)
{
    // RCPU CT: confidential addresses ("rcpux1...") must be recognized before
    // the generic bech32 path below, otherwise their HRP would be rejected as
    // an "Invalid or unsupported prefix" for the plain "rcpu" HRP.
    if (IsConfidentialAddress(str, params)) {
        CTxDestination dest;
        CPubKey pubkey;
        if (DecodeConfidentialAddress(str, params, dest, pubkey, error_str)) {
            return dest;
        }
        return CNoDestination();
    }

    std::vector<unsigned char> data;
    uint160 hash;
    error_str = "";

    // Note this will be false if it is a valid Bech32 address for a different network
    bool is_bech32 = (ToLower(str.substr(0, params.Bech32HRP().size())) == params.Bech32HRP());

    if (!is_bech32 && DecodeBase58Check(str, data, 21)) {
        // base58-encoded Bitcoin addresses.
        // Public-key-hash-addresses have version 0 (or 111 testnet).
        // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
        const std::vector<unsigned char>& pubkey_prefix = params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
        if (data.size() == hash.size() + pubkey_prefix.size() && std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin())) {
            std::copy(data.begin() + pubkey_prefix.size(), data.end(), hash.begin());
            return PKHash(hash);
        }
        // Script-hash-addresses have version 5 (or 196 testnet).
        // The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
        const std::vector<unsigned char>& script_prefix = params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
        if (data.size() == hash.size() + script_prefix.size() && std::equal(script_prefix.begin(), script_prefix.end(), data.begin())) {
            std::copy(data.begin() + script_prefix.size(), data.end(), hash.begin());
            return ScriptHash(hash);
        }

        // If the prefix of data matches either the script or pubkey prefix, the length must have been wrong
        if ((data.size() >= script_prefix.size() &&
                std::equal(script_prefix.begin(), script_prefix.end(), data.begin())) ||
            (data.size() >= pubkey_prefix.size() &&
                std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin()))) {
            error_str = "Invalid length for Base58 address (P2PKH or P2SH)";
        } else {
            error_str = "Invalid or unsupported Base58-encoded address.";
        }
        return CNoDestination();
    } else if (!is_bech32) {
        // Try Base58 decoding without the checksum, using a much larger max length
        if (!DecodeBase58(str, data, 100)) {
            error_str = "Invalid or unsupported Segwit (Bech32) or Base58 encoding.";
        } else {
            error_str = "Invalid checksum or length of Base58 address (P2PKH or P2SH)";
        }
        return CNoDestination();
    }

    data.clear();
    const auto dec = bech32::Decode(str);
    if (dec.encoding == bech32::Encoding::BECH32 || dec.encoding == bech32::Encoding::BECH32M) {
        if (dec.data.empty()) {
            error_str = "Empty Bech32 data section";
            return CNoDestination();
        }
        // Bech32 decoding
        if (dec.hrp != params.Bech32HRP()) {
            error_str = strprintf("Invalid or unsupported prefix for Segwit (Bech32) address (expected %s, got %s).", params.Bech32HRP(), dec.hrp);
            return CNoDestination();
        }
        int version = dec.data[0]; // The first 5 bit symbol is the witness version (0-16)
        if (version == 0 && dec.encoding != bech32::Encoding::BECH32) {
            error_str = "Version 0 witness address must use Bech32 checksum";
            return CNoDestination();
        }
        if (version != 0 && dec.encoding != bech32::Encoding::BECH32M) {
            error_str = "Version 1+ witness address must use Bech32m checksum";
            return CNoDestination();
        }
        // The rest of the symbols are converted witness program bytes.
        data.reserve(((dec.data.size() - 1) * 5) / 8);
        if (ConvertBits<5, 8, false>([&](unsigned char c) { data.push_back(c); }, dec.data.begin() + 1, dec.data.end())) {

            std::string_view byte_str{data.size() == 1 ? "byte" : "bytes"};

            if (version == 0) {
                {
                    WitnessV0KeyHash keyid;
                    if (data.size() == keyid.size()) {
                        std::copy(data.begin(), data.end(), keyid.begin());
                        return keyid;
                    }
                }
                {
                    WitnessV0ScriptHash scriptid;
                    if (data.size() == scriptid.size()) {
                        std::copy(data.begin(), data.end(), scriptid.begin());
                        return scriptid;
                    }
                }

                error_str = strprintf("Invalid Bech32 v0 address program size (%d %s), per BIP141", data.size(), byte_str);
                return CNoDestination();
            }

            if (version == 1 && data.size() == WITNESS_V1_TAPROOT_SIZE) {
                static_assert(WITNESS_V1_TAPROOT_SIZE == WitnessV1Taproot::size());
                WitnessV1Taproot tap;
                std::copy(data.begin(), data.end(), tap.begin());
                return tap;
            }

            if (version > 16) {
                error_str = "Invalid Bech32 address witness version";
                return CNoDestination();
            }

            if (data.size() < 2 || data.size() > BECH32_WITNESS_PROG_MAX_LEN) {
                error_str = strprintf("Invalid Bech32 address program size (%d %s)", data.size(), byte_str);
                return CNoDestination();
            }

            return WitnessUnknown{version, data};
        } else {
            error_str = strprintf("Invalid padding in Bech32 data section");
            return CNoDestination();
        }
    }

    // Perform Bech32 error location
    auto res = bech32::LocateErrors(str);
    error_str = res.first;
    if (error_locations) *error_locations = std::move(res.second);
    return CNoDestination();
}
} // namespace

CKey DecodeSecret(const std::string& str)
{
    CKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data, 34)) {
        const std::vector<unsigned char>& privkey_prefix = Params().Base58Prefix(CChainParams::SECRET_KEY);
        if ((data.size() == 32 + privkey_prefix.size() || (data.size() == 33 + privkey_prefix.size() && data.back() == 1)) &&
            std::equal(privkey_prefix.begin(), privkey_prefix.end(), data.begin())) {
            bool compressed = data.size() == 33 + privkey_prefix.size();
            key.Set(data.begin() + privkey_prefix.size(), data.begin() + privkey_prefix.size() + 32, compressed);
        }
    }
    if (!data.empty()) {
        memory_cleanse(data.data(), data.size());
    }
    return key;
}

std::string EncodeSecret(const CKey& key)
{
    // RCPU mainnet is bech32-only; do not emit WIF (base58check) either.
    if (Params().GetChainType() == ChainType::RCPUMAIN) return {};
    assert(key.IsValid());
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::SECRET_KEY);
    data.insert(data.end(), UCharCast(key.begin()), UCharCast(key.end()));
    if (key.IsCompressed()) {
        data.push_back(1);
    }
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

CExtPubKey DecodeExtPubKey(const std::string& str)
{
    CExtPubKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data, 78)) {
        const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtPubKey(const CExtPubKey& key)
{
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    return ret;
}

CExtKey DecodeExtKey(const std::string& str)
{
    CExtKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data, 78)) {
        const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtKey(const CExtKey& key)
{
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

std::string EncodeDestination(const CTxDestination& dest)
{
    return std::visit(DestinationEncoder(Params()), dest);
}

CTxDestination DecodeDestination(const std::string& str, std::string& error_msg, std::vector<int>* error_locations)
{
    return DecodeDestination(str, Params(), error_msg, error_locations);
}

CTxDestination DecodeDestination(const std::string& str)
{
    std::string error_msg;
    return DecodeDestination(str, error_msg);
}

bool IsValidDestinationString(const std::string& str, const CChainParams& params)
{
    std::string error_msg;
    return IsValidDestination(DecodeDestination(str, params, error_msg, nullptr));
}

bool IsValidDestinationString(const std::string& str)
{
    return IsValidDestinationString(str, Params());
}

bool IsConfidentialAddress(const std::string& str, const CChainParams& params)
{
    const std::string& hrp = params.ConfidentialBech32HRP();
    return str.size() > hrp.size() + 1 &&
           ToLower(str.substr(0, hrp.size())) == hrp &&
           str[hrp.size()] == '1';
}

std::string EncodeConfidentialAddress(const CTxDestination& dest, const CPubKey& pubkey, const CChainParams& params)
{
    const WitnessV0KeyHash* keyid = std::get_if<WitnessV0KeyHash>(&dest);
    if (keyid == nullptr) {
        return "";
    }
    if (!pubkey.IsValid() || !pubkey.IsCompressed()) {
        return "";
    }
    // HASH160(pubkey) must match the witness program of the destination: the
    // spend key doubles as the blinding key, so an address that disagrees
    // with this relation is malformed and must never be encoded.
    if (pubkey.GetID() != ToKeyID(*keyid)) {
        return "";
    }

    std::vector<unsigned char> payload;
    payload.reserve(1 + CONF_ADDR_PROGRAM_LEN + CONF_ADDR_PUBKEY_LEN);
    payload.push_back(CONF_ADDR_VERSION_P2WPKH);
    payload.insert(payload.end(), keyid->begin(), keyid->end());
    payload.insert(payload.end(), pubkey.begin(), pubkey.end());

    std::vector<unsigned char> data;
    if (!ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, payload.begin(), payload.end())) {
        return "";
    }
    // bech32::Encode has no length limit (only bech32::Decode rejects > 90 chars).
    return bech32::Encode(bech32::Encoding::BECH32M, params.ConfidentialBech32HRP(), data);
}

bool DecodeConfidentialAddress(const std::string& str, const CChainParams& params, CTxDestination& dest, CPubKey& pubkey, std::string& error_str)
{
    dest = CNoDestination();
    pubkey = CPubKey();
    error_str = "";

    std::string hrp;
    std::vector<unsigned char> data;
    if (!ConfBech32mDecode(hrp, data, str)) {
        error_str = "Invalid checksum or encoding of confidential address";
        return false;
    }
    if (hrp != params.ConfidentialBech32HRP()) {
        error_str = strprintf("Invalid or unsupported prefix for confidential address (expected %s, got %s).", params.ConfidentialBech32HRP(), hrp);
        return false;
    }

    std::vector<unsigned char> payload;
    if (!ConvertBits<5, 8, false>([&](unsigned char c) { payload.push_back(c); }, data.begin(), data.end())) {
        error_str = "Invalid padding in confidential address data";
        return false;
    }
    if (payload.size() != 1 + CONF_ADDR_PROGRAM_LEN + CONF_ADDR_PUBKEY_LEN) {
        error_str = strprintf("Invalid payload size for confidential address (%d bytes)", payload.size());
        return false;
    }
    if (payload[0] != CONF_ADDR_VERSION_P2WPKH) {
        error_str = strprintf("Unsupported confidential address version (%d)", payload[0]);
        return false;
    }

    WitnessV0KeyHash spend;
    std::copy(payload.begin() + 1, payload.begin() + 1 + CONF_ADDR_PROGRAM_LEN, spend.begin());
    pubkey = CPubKey(payload.begin() + 1 + CONF_ADDR_PROGRAM_LEN, payload.end());
    if (!pubkey.IsValid() || !pubkey.IsCompressed()) {
        error_str = "Invalid public key in confidential address";
        return false;
    }
    // The address embeds a single key used both for spending (witness program)
    // and ECDH blinding; enforce that relation so no two-key confusion exists.
    if (pubkey.GetID() != ToKeyID(spend)) {
        error_str = "Public key does not match the spend program hash of the confidential address";
        return false;
    }

    dest = ConfidentialKeyHash(spend, pubkey);
    return true;
}
