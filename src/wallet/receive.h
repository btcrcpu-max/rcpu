// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RECEIVE_H
#define BITCOIN_WALLET_RECEIVE_H

#include <consensus/amount.h>
#include <wallet/transaction.h>
#include <wallet/types.h>
#include <wallet/wallet.h>

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
