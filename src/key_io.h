// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEY_IO_H
#define BITCOIN_KEY_IO_H

#include <addresstype.h>
#include <chainparams.h>
#include <key.h>
#include <pubkey.h>

#include <string>

CKey DecodeSecret(const std::string& str);
std::string EncodeSecret(const CKey& key);

CExtKey DecodeExtKey(const std::string& str);
std::string EncodeExtKey(const CExtKey& extkey);
CExtPubKey DecodeExtPubKey(const std::string& str);
std::string EncodeExtPubKey(const CExtPubKey& extpubkey);

std::string EncodeDestination(const CTxDestination& dest);
CTxDestination DecodeDestination(const std::string& str);
CTxDestination DecodeDestination(const std::string& str, std::string& error_msg, std::vector<int>* error_locations = nullptr);
bool IsValidDestinationString(const std::string& str);
bool IsValidDestinationString(const std::string& str, const CChainParams& params);

// RCPU CT: confidential addresses ("rcpux1..."). A confidential address
// encodes the witness program of the spend script together with the
// recipient's (compressed) public key used for ECDH blinding (path B),
// so senders never need to ask for a separate blinding key.
//
// Byte layout of the payload (before bech32m conversion):
//   [0]               address version (0x00 = P2WPKH; reserved for future types)
//   [1..20]           witness program (20 bytes for P2WPKH)
//   [21..53]          compressed recipient public key (33 bytes)
//
// The blinding key is the spend key itself: the wallet that owns the spend
// key can unblind path-B outputs with its own private key, no extra key
// storage or recovery step is required (see doc/confidential-address.md).
std::string EncodeConfidentialAddress(const CTxDestination& dest, const CPubKey& pubkey, const CChainParams& params);
bool DecodeConfidentialAddress(const std::string& str, const CChainParams& params, CTxDestination& dest, CPubKey& pubkey, std::string& error_str);
bool IsConfidentialAddress(const std::string& str, const CChainParams& params);

#endif // BITCOIN_KEY_IO_H
