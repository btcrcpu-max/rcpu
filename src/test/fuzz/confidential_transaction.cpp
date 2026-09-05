// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>

#include <cstdint>
#include <optional>
#include <vector>

FUZZ_TARGET(confidential_transaction)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    // Deserialization switches to CT serialization automatically when
    // nVersion >= CT_VERSION (see UnserializeTransaction), so arbitrary input
    // exercises both the legacy and confidential transaction formats.
    const std::optional<CMutableTransaction> mutable_tx =
        ConsumeDeserializable<CMutableTransaction>(fuzzed_data_provider, TX_WITH_WITNESS);
    if (!mutable_tx) {
        return;
    }
    const CTransaction tx{*mutable_tx};
    (void)tx.GetHash();
    (void)tx.GetWitnessHash();
    (void)tx.ToString();
    (void)tx.IsCoinBase();
    (void)tx.IsNull();
    (void)tx.HasWitness();
    (void)tx.GetTotalSize();
    try {
        (void)tx.GetValueOut();
    } catch (const std::runtime_error&) {
    }
    // Round-trip serialization must not crash for any deserialized tx.
    try {
        DataStream ss{};
        ss << TX_WITH_WITNESS(tx);
    } catch (const std::ios_base::failure&) {
    }
}
