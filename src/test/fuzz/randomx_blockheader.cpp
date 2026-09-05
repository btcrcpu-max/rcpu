// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <uint256.h>

#include <cstdint>
#include <optional>

FUZZ_TARGET(randomx_blockheader)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const std::optional<CBlockHeader> block_header = ConsumeDeserializable<CBlockHeader>(fuzzed_data_provider);
    if (!block_header) {
        return;
    }
    (void)block_header->GetHash();
    (void)block_header->GetBlockTime();
    (void)block_header->IsNull();
    // GetRandomXCommitment is a pure function over the header; exercise it on
    // arbitrary (including null hashRandomX) inputs.
    (void)GetRandomXCommitment(*block_header);
    // Round-trip serialization must not crash for any deserialized header.
    try {
        DataStream ss{};
        ss << *block_header;
    } catch (const std::ios_base::failure&) {
    }
}
