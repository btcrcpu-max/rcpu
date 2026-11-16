// Copyright (c) 2019-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/mining.h>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <node/context.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <test/util/script.h>
#include <util/check.h>
#include <validation.h>
#include <validationinterface.h>
#include <versionbits.h>

#include <limits>

using node::BlockAssembler;
using node::NodeContext;

bool SolveBlock(CBlockHeader& block, int64_t height, const Consensus::Params& params, uint32_t prev_block_time)
{
    if (!params.fPowRandomX) {
        while (!CheckProofOfWork(block.GetHash(), block.nBits, params)) {
            if (block.nNonce == std::numeric_limits<uint32_t>::max()) return false;
            ++block.nNonce;
        }
        return true;
    }

    const std::optional<PowAlgo> algo{GetPowAlgoForHeader(height, block.nVersion, params)};
    if (!algo) return false;

    const auto check_pow = [&](POWVerifyMode mode, uint256* out_hash) {
        switch (*algo) {
        case PowAlgo::RandomX:
            return CheckProofOfWorkRandomX(block, params, mode, out_hash, prev_block_time);
        case PowAlgo::Etchash:
            return CheckProofOfWorkEtchash(block, height, params, mode, out_hash);
        case PowAlgo::QHash:
            return CheckProofOfWorkQHash(block, params, mode, out_hash);
        }
        return false;
    };

    while (true) {
        uint256 solved;
        if (check_pow(POW_VERIFY_MINING, &solved)) {
            block.hashRandomX = solved;
            return check_pow(POW_VERIFY_FULL, nullptr);
        }
        if (block.nNonce == std::numeric_limits<uint32_t>::max()) return false;
        ++block.nNonce;
    }
}

COutPoint generatetoaddress(const NodeContext& node, const std::string& address)
{
    const auto dest = DecodeDestination(address);
    assert(IsValidDestination(dest));
    const auto coinbase_script = GetScriptForDestination(dest);

    return MineBlock(node, coinbase_script);
}

std::vector<std::shared_ptr<CBlock>> CreateBlockChain(size_t total_height, const CChainParams& params)
{
    std::vector<std::shared_ptr<CBlock>> ret{total_height};
    auto time{params.GenesisBlock().nTime};
    for (size_t height{0}; height < total_height; ++height) {
        CBlock& block{*(ret.at(height) = std::make_shared<CBlock>())};

        CMutableTransaction coinbase_tx;
        coinbase_tx.vin.resize(1);
        coinbase_tx.vin[0].prevout.SetNull();
        coinbase_tx.vout.resize(1);
        coinbase_tx.vout[0].scriptPubKey = P2WSH_OP_TRUE;
        coinbase_tx.vout[0].nValue = GetBlockSubsidy(height + 1, params.GetConsensus());
        coinbase_tx.vin[0].scriptSig = CScript() << (height + 1) << OP_0;
        block.vtx = {MakeTransactionRef(std::move(coinbase_tx))};

        block.nVersion = VERSIONBITS_LAST_OLD_BLOCK_VERSION;
        block.hashPrevBlock = (height >= 1 ? *ret.at(height - 1) : params.GenesisBlock()).GetHash();
        block.hashMerkleRoot = BlockMerkleRoot(block);
        block.nTime = ++time;
        block.nBits = params.GenesisBlock().nBits;
        block.nNonce = 0;

        const uint32_t prev_block_time{height >= 1 ? ret.at(height - 1)->nTime : params.GenesisBlock().nTime};
        assert(SolveBlock(block, height + 1, params.GetConsensus(), prev_block_time));
    }
    return ret;
}

COutPoint MineBlock(const NodeContext& node, const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(node, coinbase_scriptPubKey);
    auto valid = MineBlock(node, block);
    assert(!valid.IsNull());
    return valid;
}

struct BlockValidationStateCatcher : public CValidationInterface {
    const uint256 m_hash;
    std::optional<BlockValidationState> m_state;

    BlockValidationStateCatcher(const uint256& hash)
        : m_hash{hash},
          m_state{} {}

protected:
    void BlockChecked(const CBlock& block, const BlockValidationState& state) override
    {
        if (block.GetHash() != m_hash) return;
        m_state = state;
    }
};

COutPoint MineBlock(const NodeContext& node, std::shared_ptr<CBlock>& block)
{
    auto& chainman{*Assert(node.chainman)};
    const auto old_height = WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight());
    const uint32_t prev_block_time = WITH_LOCK(chainman.GetMutex(), return chainman.ActiveChain().Tip()->GetBlockTime());
    if (!SolveBlock(*block, old_height + 1, Params().GetConsensus(), prev_block_time)) return {};

    bool new_block;
    BlockValidationStateCatcher bvsc{block->GetHash()};
    RegisterValidationInterface(&bvsc);
    const bool processed{chainman.ProcessNewBlock(block, true, true, &new_block)};
    const bool duplicate{!new_block && processed};
    assert(!duplicate);
    UnregisterValidationInterface(&bvsc);
    SyncWithValidationInterfaceQueue();
    const bool was_valid{bvsc.m_state && bvsc.m_state->IsValid()};
    assert(old_height + was_valid == WITH_LOCK(chainman.GetMutex(), return chainman.ActiveHeight()));

    if (was_valid) return {block->vtx[0]->GetHash(), 0};
    return {};
}

std::shared_ptr<CBlock> PrepareBlock(const NodeContext& node, const CScript& coinbase_scriptPubKey,
                                     const BlockAssembler::Options& assembler_options)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Assert(node.chainman)->ActiveChainstate(), Assert(node.mempool.get()), assembler_options}
            .CreateNewBlock(coinbase_scriptPubKey)
            ->block);

    LOCK(cs_main);
    block->nTime = Assert(node.chainman)->ActiveChain().Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    return block;
}
std::shared_ptr<CBlock> PrepareBlock(const NodeContext& node, const CScript& coinbase_scriptPubKey)
{
    BlockAssembler::Options assembler_options;
    ApplyArgsManOptions(*node.args, assembler_options);
    return PrepareBlock(node, coinbase_scriptPubKey, assembler_options);
}
