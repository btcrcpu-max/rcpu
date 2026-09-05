// Copyright (c) 2023 The Bitcoin Core developers
// Copyright (c) 2024 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/chaintype.h>

#include <cassert>
#include <optional>
#include <string>

std::string ChainTypeToString(ChainType chain)
{
    switch (chain) {
    case ChainType::MAIN:
        return "main";
    case ChainType::TESTNET:
        return "test";
    case ChainType::SIGNET:
        return "signet";
    case ChainType::REGTEST:
        return "regtest";
    // !RCPU
    case ChainType::RCPUMAIN:
        return "rcpu";
    case ChainType::RCPUTESTNET:
        return "rcputestnet";
    case ChainType::RCPUREGTEST:
        return "rcpuregtest";
    // !RCPU END
    }
    assert(false);
}

std::optional<ChainType> ChainTypeFromString(std::string_view chain)
{
    if (chain == "main") {
        return ChainType::MAIN;
    } else if (chain == "test") {
        return ChainType::TESTNET;
    } else if (chain == "signet") {
        return ChainType::SIGNET;
    } else if (chain == "regtest") {
        return ChainType::REGTEST;
    // !RCPU
    } else if (chain == "rcpu") {
        return ChainType::RCPUMAIN;
    } else if (chain == "rcputestnet") {
        return ChainType::RCPUTESTNET;
    } else if (chain == "rcpuregtest") {
        return ChainType::RCPUREGTEST;
    // !RCPU END
    } else {
        return std::nullopt;
    }
}
