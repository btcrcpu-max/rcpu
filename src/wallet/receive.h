// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RECEIVE_H
#define BITCOIN_WALLET_RECEIVE_H

#include <consensus/amount.h>
#include <wallet/transaction.h>
#include <wallet/types.h>
#include <wallet/wallet.h>

#include <cstdint>
#include <optional>

namespace wallet {
isminetype InputIsMine(const CWallet& wallet, const CTxIn& txin) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);

//! RCPU CT (N-01): unblind a confidential output using this wallet's keys.
//! Dispatches on the nonce-commitment encoding: 33-byte 0x02-prefixed legacy
//! path-A plaintext nonce goes through UnblindValue (range-proof rewind, no
//! key needed); 33-byte 0x03-prefixed path-B ECDH ephemeral pubkey goes
//! through UnblindValueWithKey using the private key resolved from the output
//! script. Returns false when the output is confidential but cannot be
//! unblinded (bad commitment shape, missing key or failed rewind), so callers
//! never conflate a failure with a truthful zero amount.
bool UnblindWalletOutput(const CWallet& wallet, const CTxOut& txout, CAmount& value_out, uint256& blind_out);

/** Returns whether all of the inputs match the filter */
bool AllInputsMine(const CWallet& wallet, const CTransaction& tx, const isminefilter& filter);

/**
 * RCPU CT 1.0.18: unblind a confidential output using this wallet.
 *
 * Tries the legacy plaintext-nonce rewind (path A, UnblindValue) first;
 * if that fails, recovers the amount via the recipient-ECDH path B
 * (UnblindValueWithKey) with this wallet's own private key for the output
 * script. Explicit amounts pass through unchanged. Returns false only when
 * the output genuinely cannot be unblinded by this wallet (foreign scripts,
 * P2SH/P2WSH/Taproot, missing private key) so callers keep reporting 0 for
 * those -- an unblind failure is never conflated with a real zero amount.
 */
bool UnblindConfidentialOutput(const CWallet& wallet, const CTxOut& txout,
                               CAmount& value, uint256& blind);

/**
 * RCPU CT: why (and whether) the wallet can value a confidential output.
 *
 * Callers must render `std::nullopt` as "unknown", never as 0 -- that
 * conflation is exactly what made a locked or watch-only wallet report a
 * zero balance for outputs it owns.
 */
enum class CTAmountStatus : uint8_t {
    Explicit = 0, //!< plaintext amount, always readable
    Known,        //!< confidential, unblinded with our key just now
    KnownCached,  //!< confidential, plaintext recovered from wallet metadata
    Locked,       //!< encrypted wallet is locked, key unavailable
    NoKey,        //!< watch-only, or no key can ever unblind this script
    Unreadable,   //!< key present but the output still cannot be unblinded
};

struct CTOutputInfo {
    CTAmountStatus status{CTAmountStatus::Unreadable};
    std::optional<CAmount> amount{};
};

//! Stable identifier for RPC / logs ("explicit", "known", "locked", ...).
const char* CTAmountStatusName(CTAmountStatus status);

//! Value one output, without consulting wallet metadata.
CTOutputInfo GetCTOutputInfo(const CWallet& wallet, const CTxOut& txout);

//! Value one output of a wallet transaction, falling back to the plaintext
//! amount cached while the wallet was unlocked (unencrypted wallets only).
CTOutputInfo GetCTOutputInfo(const CWallet& wallet, const CWalletTx& wtx, unsigned int n);

CAmount OutputGetCredit(const CWallet& wallet, const CTxOut& txout, const isminefilter& filter);
CAmount TxGetCredit(const CWallet& wallet, const CTransaction& tx, const isminefilter& filter);

bool ScriptIsChange(const CWallet& wallet, const CScript& script) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
bool OutputIsChange(const CWallet& wallet, const CTxOut& txout) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
CAmount OutputGetChange(const CWallet& wallet, const CTxOut& txout) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
CAmount TxGetChange(const CWallet& wallet, const CTransaction& tx);

CAmount CachedTxGetCredit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
//! filter decides which addresses will count towards the debit
CAmount CachedTxGetDebit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter);
CAmount CachedTxGetChange(const CWallet& wallet, const CWalletTx& wtx);
CAmount CachedTxGetImmatureCredit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
CAmount CachedTxGetAvailableCredit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter = ISMINE_SPENDABLE)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
struct COutputEntry
{
    CTxDestination destination;
    CAmount amount;
    int vout;
};
void CachedTxGetAmounts(const CWallet& wallet, const CWalletTx& wtx,
                        std::list<COutputEntry>& listReceived,
                        std::list<COutputEntry>& listSent,
                        CAmount& nFee, const isminefilter& filter,
                        bool include_change);
bool CachedTxIsFromMe(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter);
bool CachedTxIsTrusted(const CWallet& wallet, const CWalletTx& wtx, std::set<uint256>& trusted_parents) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
bool CachedTxIsTrusted(const CWallet& wallet, const CWalletTx& wtx);

struct Balance {
    CAmount m_mine_trusted{0};           //!< Trusted, at depth=GetBalance.min_depth or more
    CAmount m_mine_untrusted_pending{0}; //!< Untrusted, but in mempool (pending)
    CAmount m_mine_immature{0};          //!< Immature coinbases in the main chain
    CAmount m_watchonly_trusted{0};
    CAmount m_watchonly_untrusted_pending{0};
    CAmount m_watchonly_immature{0};
};
Balance GetBalance(const CWallet& wallet, int min_depth = 0, bool avoid_reuse = true);

std::map<CTxDestination, CAmount> GetAddressBalances(const CWallet& wallet);
std::set<std::set<CTxDestination>> GetAddressGroupings(const CWallet& wallet) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet);
} // namespace wallet

#endif // BITCOIN_WALLET_RECEIVE_H
