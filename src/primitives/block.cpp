// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2024 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>

// !RCPU
std::atomic<bool> g_isRandomX{false};   // global
std::atomic<bool> g_isIBDFinished{false};    // global
// !RCPU END

uint256 CBlockHeader::GetHash() const
{
    // !RCPU
    // Canonical RCPU hash domain (P1-1): on RCPU chains the hash input is the
    // fixed 112-byte serialized header including hashRandomX, written field by
    // field instead of via the SERIALIZE_METHODS global-flag branch. The
    // predicate (A-02) is the chain-level fPowRandomX mirror: 112 bytes on RCPU
    // mainnet/testnet/regtest (all run with g_isRandomX == true), 80 bytes on
    // Bitcoin-compatible chains (testnet, signet, regtest) and in unit tests.
    if (HasRandomXHeaderField()) {
        HashWriter hw{};
        hw << nVersion << hashPrevBlock << hashMerkleRoot << nTime << nBits << nNonce << hashRandomX;
        return hw.GetHash();
    }
    return (HashWriter{} << *this).GetHash();
    // !RCPU END
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    // !RCPU
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, %svtx=%u)\n",
    // !RCPU END
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        // !RCPU
        HasRandomXHeaderField() ? "hashRandomX=" + hashRandomX.ToString() + ", " : "",
        // !RCPU END
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
