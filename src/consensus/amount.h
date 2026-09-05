// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Copyright (c) 2024-2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RCPU_CONSENSUS_AMOUNT_H
#define RCPU_CONSENSUS_AMOUNT_H

#include <cstdint>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

/** The amount of satoshis in one RCPU. */
static constexpr CAmount COIN = 100000000;

/** No amount larger than this (in satoshi) is valid per output.
 *
 * IMPORTANT: This is NOT a hard cap on total coin supply. It is a per-output
 * sanity check used by consensus-critical validation code to catch overflow
 * bugs. RCPU has a tail emission of 1 RCPU per block after 10 halvings,
 * meaning total supply grows slowly and indefinitely.
 *
 * The exact value is consensus critical - do not change without a hard fork.
 * */
static constexpr CAmount MAX_MONEY = 2100000000 * COIN;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // RCPU_CONSENSUS_AMOUNT_H
