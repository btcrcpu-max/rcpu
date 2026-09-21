// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/key_io_invalid.json.h>
#include <test/data/key_io_valid.json.h>

#include <key.h>
#include <key_io.h>
#include <script/script.h>
#include <test/util/json.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

BOOST_FIXTURE_TEST_SUITE(key_io_tests, BasicTestingSetup)

// Goal: check that parsed keys match test payload
BOOST_AUTO_TEST_CASE(key_io_valid_parse)
{
    UniValue tests = read_json(json_tests::key_io_valid);
    CKey privkey;
    CTxDestination destination;
    SelectParams(ChainType::MAIN);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) { // Allow for extra stuff (useful for comments)
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();
        const std::vector<std::byte> exp_payload{ParseHex<std::byte>(test[1].get_str())};
        const UniValue &metadata = test[2].get_obj();
        bool isPrivkey = metadata.find_value("isPrivkey").get_bool();
        SelectParams(ChainTypeFromString(metadata.find_value("chain").get_str()).value());
        bool try_case_flip = metadata.find_value("tryCaseFlip").isNull() ? false : metadata.find_value("tryCaseFlip").get_bool();
        if (isPrivkey) {
            bool isCompressed = metadata.find_value("isCompressed").get_bool();
            // Must be valid private key
            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(privkey.IsValid(), "!IsValid:" + strTest);
            BOOST_CHECK_MESSAGE(privkey.IsCompressed() == isCompressed, "compressed mismatch:" + strTest);
            BOOST_CHECK_MESSAGE(Span{privkey} == Span{exp_payload}, "key mismatch:" + strTest);

            // Private key must be invalid public key
            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid privkey as pubkey:" + strTest);
        } else {
            // Must be valid public key
            destination = DecodeDestination(exp_base58string);
            CScript script = GetScriptForDestination(destination);
            BOOST_CHECK_MESSAGE(IsValidDestination(destination), "!IsValid:" + strTest);
            BOOST_CHECK_EQUAL(HexStr(script), HexStr(exp_payload));

            // Try flipped case version
            for (char& c : exp_base58string) {
                if (c >= 'a' && c <= 'z') {
                    c = (c - 'a') + 'A';
                } else if (c >= 'A' && c <= 'Z') {
                    c = (c - 'A') + 'a';
                }
            }
            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(IsValidDestination(destination) == try_case_flip, "!IsValid case flipped:" + strTest);
            if (IsValidDestination(destination)) {
                script = GetScriptForDestination(destination);
                BOOST_CHECK_EQUAL(HexStr(script), HexStr(exp_payload));
            }

            // Public key must be invalid private key
            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid pubkey as privkey:" + strTest);
        }
    }
}

// Goal: check that generated keys match test vectors
BOOST_AUTO_TEST_CASE(key_io_valid_gen)
{
    UniValue tests = read_json(json_tests::key_io_valid);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();
        std::vector<unsigned char> exp_payload = ParseHex(test[1].get_str());
        const UniValue &metadata = test[2].get_obj();
        bool isPrivkey = metadata.find_value("isPrivkey").get_bool();
        SelectParams(ChainTypeFromString(metadata.find_value("chain").get_str()).value());
        if (isPrivkey) {
            bool isCompressed = metadata.find_value("isCompressed").get_bool();
            CKey key;
            key.Set(exp_payload.begin(), exp_payload.end(), isCompressed);
            assert(key.IsValid());
            BOOST_CHECK_MESSAGE(EncodeSecret(key) == exp_base58string, "result mismatch: " + strTest);
        } else {
            CTxDestination dest;
            CScript exp_script(exp_payload.begin(), exp_payload.end());
            BOOST_CHECK(ExtractDestination(exp_script, dest));
            std::string address = EncodeDestination(dest);

            BOOST_CHECK_EQUAL(address, exp_base58string);
        }
    }

    SelectParams(ChainType::MAIN);
}


// Goal: check that base58 parsing code is robust against a variety of corrupted data
BOOST_AUTO_TEST_CASE(key_io_invalid)
{
    UniValue tests = read_json(json_tests::key_io_invalid); // Negative testcases
    CKey privkey;
    CTxDestination destination;

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();

        // must be invalid as public and as private key
        for (const auto& chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET, ChainType::REGTEST}) {
            SelectParams(chain);
            destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid pubkey in mainnet:" + strTest);
            privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid privkey in mainnet:" + strTest);
        }
    }
}

// RCPU CT: confidential address (rcpux1...) round-trip and validation tests.
BOOST_AUTO_TEST_CASE(confidential_address_roundtrip)
{
    SelectParams(ChainType::RCPUMAIN);
    CKey key;
    key.MakeNewKey(true);
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(key.IsCompressed());

    // Build a ConfidentialKeyHash and encode it.
    WitnessV0KeyHash spend(key.GetPubKey());
    CPubKey pk = key.GetPubKey();
    CTxDestination conf_dest = ConfidentialKeyHash(spend, pk);
    std::string addr = EncodeDestination(conf_dest);
    BOOST_CHECK(addr.substr(0, 6) == "rcpux1");

    // Decode must restore the exact destination.
    std::string error_str;
    CTxDestination decoded = DecodeDestination(addr, error_str);
    BOOST_CHECK_MESSAGE(IsValidDestination(decoded), error_str);
    const auto* ckh = std::get_if<ConfidentialKeyHash>(&decoded);
    BOOST_REQUIRE_MESSAGE(ckh != nullptr, "Decoded is not ConfidentialKeyHash");
    BOOST_CHECK(ckh->GetSpend() == spend);
    BOOST_CHECK(ckh->GetBlinding() == pk);

    // Re-encode must produce the same address.
    BOOST_CHECK_EQUAL(EncodeDestination(decoded), addr);

    // Regtest HRP must differ.
    SelectParams(ChainType::RCPUREGTEST);
    CTxDestination reg_dest = ConfidentialKeyHash(spend, pk);
    std::string reg_addr = EncodeDestination(reg_dest);
    BOOST_CHECK(reg_addr.substr(0, 7) == "rrcpux1");
}

BOOST_AUTO_TEST_CASE(confidential_address_wrong_hrp)
{
    // A mainnet rcpux1 address must be invalid on RCPU regtest, and vice versa.
    SelectParams(ChainType::RCPUMAIN);
    CKey key;
    key.MakeNewKey(true);
    WitnessV0KeyHash spend(key.GetPubKey());
    std::string main_addr = EncodeDestination(ConfidentialKeyHash(spend, key.GetPubKey()));

    std::string error_str;
    SelectParams(ChainType::RCPUREGTEST);
    BOOST_CHECK_MESSAGE(!IsValidDestination(DecodeDestination(main_addr, error_str)),
                        error_str);
}

BOOST_AUTO_TEST_CASE(confidential_address_uncompressed)
{
    SelectParams(ChainType::RCPUMAIN);
    CKey key;
    key.MakeNewKey(false);
    BOOST_REQUIRE(!key.IsCompressed());

    WitnessV0KeyHash spend(key.GetPubKey());
    // Encoding must refuse to produce a confidential address for uncompressed keys.
    std::string addr = EncodeConfidentialAddress(spend, key.GetPubKey(), Params());
    BOOST_CHECK(addr.empty());
}

BOOST_AUTO_TEST_CASE(confidential_address_hash_mismatch)
{
    SelectParams(ChainType::RCPUMAIN);
    CKey spend_key;
    spend_key.MakeNewKey(true);
    CKey blind_key;
    blind_key.MakeNewKey(true);

    WitnessV0KeyHash spend(spend_key.GetPubKey());
    // pubkey from a different key must not match the spend hash.
    std::string addr = EncodeConfidentialAddress(spend, blind_key.GetPubKey(), Params());
    BOOST_CHECK(addr.empty());

    // Decoding a crafted payload with a mismatching pubkey must also fail.
    // We construct a valid-looking address first, then swap the pubkey bytes.
    std::string valid = EncodeConfidentialAddress(spend, spend_key.GetPubKey(), Params());
    BOOST_REQUIRE(!valid.empty());
    // Corrupt one pubkey byte in the data part (near the end, after the version + 20-byte hash).
    valid[valid.size() - 5] = (valid[valid.size() - 5] == 'a') ? 'b' : 'a';
    std::string error_str;
    CTxDestination decoded = DecodeDestination(valid, error_str);
    BOOST_CHECK_MESSAGE(!IsValidDestination(decoded), error_str);
}

BOOST_AUTO_TEST_SUITE_END()
