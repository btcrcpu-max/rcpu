// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <consensus/amount.h>
#include <blind.h>
#include <consensus/consensus.h>
#include <key.h>
#include <key_io.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <wallet/receive.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>

namespace wallet {

// RCPU CT (N-01): unblind a confidential output belonging to this wallet.
// Dispatches on the nonce-commitment encoding (see IsLegacyNonceCommit in
// blind.cpp):
//   - explicit outputs are returned as-is;
//   - 33-byte 0x02-prefixed commitments (path A, plaintext nonce) rewind the
//     range proof with UnblindValue -- no key needed;
//   - 33-byte 0x03-prefixed commitments (path B, ECDH ephemeral pubkey) are
//     unblinded with UnblindValueWithKey using the private key resolved from
//     the output script (P2PK / P2PKH / P2WPKH / P2TR only; scripted outputs
//     have no single signing key and fail closed);
//   - any other commitment shape fails closed.
// On failure the output is not ours / not unblindable: callers must treat the
// output as worth zero and never conflate failure with a truthful zero.
bool UnblindWalletOutput(const CWallet& wallet, const CTxOut& txout, CAmount& value_out, uint256& blind_out)
{
    if (txout.nValue.IsExplicit()) {
        value_out = txout.nValue.GetAmount();
        blind_out = uint256();
        return true;
    }
    const auto& nc = txout.nNonce.vchCommitment;
    if (nc.size() == 33 && nc[0] == 0x02) {
        return UnblindValue(txout.nValue, txout.nNonce, txout.vchRangeproof, value_out, blind_out);
    }
    if (nc.size() == 33 && nc[0] == 0x03) {
        CTxDestination dest;
        // Note: ExtractDestination returns false for bare P2PK even though it
        // fills in a valid PubKeyDestination (P2PK has no address form); only
        // a CNoDestination means the script carries no key we can use.
        ExtractDestination(txout.scriptPubKey, dest);
        if (std::get_if<CNoDestination>(&dest)) return false;
        CKey key;
        CKeyID keyid;
        bool is_taproot = false;
        {
            // Key lookup touches the wallet's SPK caches; the callers do not
            // all hold cs_wallet at this point (e.g. OutputGetCredit unblinds
            // before taking the lock), so take it here. RecursiveMutex makes
            // re-entering safe when callers already hold it.
            LOCK(wallet.cs_wallet);
            if (const auto* pk = std::get_if<PubKeyDestination>(&dest)) {
                keyid = pk->GetPubKey().GetID();
            } else if (const auto* pkh = std::get_if<PKHash>(&dest)) {
                keyid = ToKeyID(*pkh);
            } else if (const auto* wpkh = std::get_if<WitnessV0KeyHash>(&dest)) {
                keyid = ToKeyID(*wpkh);
            } else if (const auto* tr = std::get_if<WitnessV1Taproot>(&dest)) {
                is_taproot = true;
                for (const auto* spkman : wallet.GetScriptPubKeyMans(txout.scriptPubKey)) {
                    // GetSolvingProvider omits private keys for descriptor
                    // SPKMs (GetSigningProvider with include_private=false), so
                    // GetKeyByXOnly would fail and the encrypted Path-B output
                    // would unblind to 0. Use the public GetPrivKey accessor to
                    // fetch the private key instead.
                    if (const auto* desc = dynamic_cast<const DescriptorScriptPubKeyMan*>(spkman)) {
                        CKeyID kd;
                        CKey k;
                        if (desc->GetPrivKey(txout.scriptPubKey, kd, k) && k.IsValid()) { key = k; break; }
                    } else {
                        const auto provider = spkman->GetSolvingProvider(txout.scriptPubKey);
                        if (provider && provider->GetKeyByXOnly(*tr, key)) break;
                    }
                }
            } else {
                // P2SH / P2WSH / unknown witness programs / bare scripts: no single key.
                return false;
            }
            if (!is_taproot) {
                for (const auto* spkman : wallet.GetScriptPubKeyMans(txout.scriptPubKey)) {
                    // LegacySigningProvider deliberately never returns private keys
                    // (GetKey is hard-wired to false upstream), so for legacy
                    // wallets we must resolve the key straight from the SPKM.
                    if (const auto* legacy = dynamic_cast<const LegacyScriptPubKeyMan*>(spkman)) {
                        if (legacy->GetKey(keyid, key)) break;
                        continue;
                    }
                    // Descriptor SPKMs keep private keys behind a separate accessor;
                    // the solving provider intentionally omits them, so try GetPrivKey first.
                    if (const auto* desc = dynamic_cast<const DescriptorScriptPubKeyMan*>(spkman)) {
                        CKeyID kd;
                        CKey k;
                        if (desc->GetPrivKey(txout.scriptPubKey, kd, k) && k.IsValid()) { key = k; break; }
                        continue;
                    }
                    const auto provider = spkman->GetSolvingProvider(txout.scriptPubKey);
                    if (provider && provider->GetKey(keyid, key)) break;
                }
            }
        }
        if (!key.IsValid()) {
            wallet.WalletLogPrintf("unblind path-B failed: no privkey script=%s locked=%d\n",
                                   HexStr(txout.scriptPubKey), wallet.IsLocked());
            return false;
        }
        return UnblindValueWithKey(key, txout.nValue, txout.nNonce, txout.vchRangeproof, value_out, blind_out);
    }
    return false; // malformed commitment
}

isminetype InputIsMine(const CWallet& wallet, const CTxIn& txin)
{
    AssertLockHeld(wallet.cs_wallet);
    const CWalletTx* prev = wallet.GetWalletTx(txin.prevout.hash);
    if (prev && txin.prevout.n < prev->tx->vout.size()) {
        return wallet.IsMine(prev->tx->vout[txin.prevout.n]);
    }
    return ISMINE_NO;
}

bool AllInputsMine(const CWallet& wallet, const CTransaction& tx, const isminefilter& filter)
{
    LOCK(wallet.cs_wallet);
    for (const CTxIn& txin : tx.vin) {
        if (!(InputIsMine(wallet, txin) & filter)) return false;
    }
    return true;
}

bool UnblindConfidentialOutput(const CWallet& wallet, const CTxOut& txout,
                               CAmount& value, uint256& blind)
{
    LOCK(wallet.cs_wallet);
    if (txout.nValue.IsExplicit()) {
        value = txout.nValue.GetAmount();
        blind = uint256();
        return true;
    }
    // Path A first: legacy plaintext-nonce rewind -- the shape a foreign
    // sender produces when it cannot resolve our public key (1.0.18 default
    // fallback).
    if (UnblindValue(txout.nValue, txout.nNonce, txout.vchRangeproof, value, blind)) {
        return true;
    }
    // Path B: the output was blinded to a recipient public key via ECDH.
    // Recover it with this wallet's own private key for the output script, so
    // change / balance / lists / spend inputs show the real amount instead of
    // 0 whenever we own the key. Outputs bound to a script with no single
    // private key (P2SH / P2WSH / Taproot / pubkey-import-only) and genuinely
    // foreign outputs stay unblinded -- the caller reports 0 as before.
    CTxDestination dest;
    if (!ExtractDestination(txout.scriptPubKey, dest)) {
        return false;
    }
    CKeyID keyid;
    bool is_taproot = false;
    if (const auto* pk = std::get_if<PubKeyDestination>(&dest)) {
        keyid = pk->GetPubKey().GetID();
    } else if (const auto* pkh = std::get_if<PKHash>(&dest)) {
        keyid = ToKeyID(*pkh);
    } else if (const auto* wpkh = std::get_if<WitnessV0KeyHash>(&dest)) {
        keyid = ToKeyID(*wpkh);
    } else if (const auto* tr = std::get_if<WitnessV1Taproot>(&dest)) {
        // Path-B (confidential, rcpux1...) outputs are bound to a taproot
        // script. Without this branch UnblindConfidentialOutput returned false
        // for every P2TR output, so receive / balance / history credited 0 --
        // even though the wallet owns the key (UnblindWalletOutput already
        // handled this case). Mirror that key resolution here.
        is_taproot = true;
    } else {
        return false;
    }
    // Fetch the private key from the managing ScriptPubKeyMan directly:
    // the solving provider exposed by CWallet::GetSolvingProvider() omits
    // private keys for DescriptorScriptPubKeyMan (GetSigningProvider with
    // include_private=false), and LegacySigningProvider::GetKey returns
    // false outright. Without the wallet's own key, Path-B (recipient-ECDH)
    // outputs -- change, balance, lists -- would all unblind to 0.
    CKey key;
    bool have_key = false;
    const auto* tr_dest = std::get_if<WitnessV1Taproot>(&dest);
    for (ScriptPubKeyMan* man : wallet.GetScriptPubKeyMans(txout.scriptPubKey)) {
        if (is_taproot) {
            // Fetch the taproot private key via the public GetPrivKey accessor,
            // mirroring UnblindWalletOutput above. GetSolvingProvider omits
            // private keys for descriptor SPKMs, so GetKeyByXOnly fails.
            if (const auto* desc = dynamic_cast<const DescriptorScriptPubKeyMan*>(man)) {
                CKeyID kd;
                if (desc->GetPrivKey(txout.scriptPubKey, kd, key)) { have_key = true; break; }
            } else {
                std::unique_ptr<SigningProvider> spk = man->GetSolvingProvider(txout.scriptPubKey);
                if (spk && tr_dest && spk->GetKeyByXOnly(*tr_dest, key)) { have_key = true; break; }
            }
        }
        // Descriptor SPKMs keep private keys behind a separate accessor; the
        // solving provider intentionally omits them, so try GetPrivKey first.
        else if (auto* desc = dynamic_cast<DescriptorScriptPubKeyMan*>(man)) {
            CKeyID kd;
            if (desc->GetPrivKey(txout.scriptPubKey, kd, key)) { have_key = true; break; }
        } else {
            std::unique_ptr<SigningProvider> spk = man->GetSolvingProvider(txout.scriptPubKey);
            if (spk && spk->GetKey(keyid, key)) { have_key = true; break; }
        }
    }
    if (!have_key) {
        return false;
    }
    return UnblindValueWithKey(key, txout.nValue, txout.nNonce, txout.vchRangeproof, value, blind);
}

CAmount OutputGetCredit(const CWallet& wallet, const CTxOut& txout, const isminefilter& filter)
{
    CAmount value;
    uint256 blind;
    if (!UnblindConfidentialOutput(wallet, txout, value, blind)) {
        return 0; // cannot unblind (not ours)
    }
    if (!MoneyRange(value))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    LOCK(wallet.cs_wallet);
    return ((wallet.IsMine(txout) & filter) ? value : 0);
}

CAmount TxGetCredit(const CWallet& wallet, const CTransaction& tx, const isminefilter& filter)
{
    CAmount nCredit = 0;
    for (const CTxOut& txout : tx.vout)
    {
        nCredit += OutputGetCredit(wallet, txout, filter);
        if (!MoneyRange(nCredit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nCredit;
}

bool ScriptIsChange(const CWallet& wallet, const CScript& script)
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    AssertLockHeld(wallet.cs_wallet);
    if (wallet.IsMine(script))
    {
        CTxDestination address;
        if (!ExtractDestination(script, address))
            return true;
        if (!wallet.FindAddressBookEntry(address)) {
            return true;
        }
    }
    return false;
}

bool OutputIsChange(const CWallet& wallet, const CTxOut& txout)
{
    return ScriptIsChange(wallet, txout.scriptPubKey);
}

CAmount OutputGetChange(const CWallet& wallet, const CTxOut& txout)
{
    AssertLockHeld(wallet.cs_wallet);
    CAmount value;
    uint256 blind;
    if (!UnblindConfidentialOutput(wallet, txout, value, blind)) {
        return 0; // cannot unblind (not ours)
    }
    if (!MoneyRange(value))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    return (OutputIsChange(wallet, txout) ? value : 0);
}

CAmount TxGetChange(const CWallet& wallet, const CTransaction& tx)
{
    LOCK(wallet.cs_wallet);
    CAmount nChange = 0;
    for (const CTxOut& txout : tx.vout)
    {
        nChange += OutputGetChange(wallet, txout);
        if (!MoneyRange(nChange))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nChange;
}

static CAmount GetCachableAmount(const CWallet& wallet, const CWalletTx& wtx, CWalletTx::AmountType type, const isminefilter& filter)
{
    auto& amount = wtx.m_amounts[type];
    if (!amount.m_cached[filter]) {
        amount.Set(filter, type == CWalletTx::DEBIT ? wallet.GetDebit(*wtx.tx, filter) : TxGetCredit(wallet, *wtx.tx, filter));
        wtx.m_is_cache_empty = false;
    }
    return amount.m_value[filter];
}

CAmount CachedTxGetCredit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
{
    AssertLockHeld(wallet.cs_wallet);

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (wallet.IsTxImmatureCoinBase(wtx))
        return 0;

    CAmount credit = 0;
    const isminefilter get_amount_filter{filter & ISMINE_ALL};
    if (get_amount_filter) {
        // GetBalance can assume transactions in mapWallet won't change
        credit += GetCachableAmount(wallet, wtx, CWalletTx::CREDIT, get_amount_filter);
    }
    return credit;
}

CAmount CachedTxGetDebit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
{
    if (wtx.tx->vin.empty())
        return 0;

    CAmount debit = 0;
    const isminefilter get_amount_filter{filter & ISMINE_ALL};
    if (get_amount_filter) {
        debit += GetCachableAmount(wallet, wtx, CWalletTx::DEBIT, get_amount_filter);
    }
    return debit;
}

CAmount CachedTxGetChange(const CWallet& wallet, const CWalletTx& wtx)
{
    if (wtx.fChangeCached)
        return wtx.nChangeCached;
    wtx.nChangeCached = TxGetChange(wallet, *wtx.tx);
    wtx.fChangeCached = true;
    return wtx.nChangeCached;
}

CAmount CachedTxGetImmatureCredit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
{
    AssertLockHeld(wallet.cs_wallet);

    if (wallet.IsTxImmatureCoinBase(wtx) && wallet.IsTxInMainChain(wtx)) {
        return GetCachableAmount(wallet, wtx, CWalletTx::IMMATURE_CREDIT, filter);
    }

    return 0;
}

CAmount CachedTxGetAvailableCredit(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
{
    AssertLockHeld(wallet.cs_wallet);

    // Avoid caching ismine for NO or ALL cases (could remove this check and simplify in the future).
    bool allow_cache = (filter & ISMINE_ALL) && (filter & ISMINE_ALL) != ISMINE_ALL;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (wallet.IsTxImmatureCoinBase(wtx))
        return 0;

    if (allow_cache && wtx.m_amounts[CWalletTx::AVAILABLE_CREDIT].m_cached[filter]) {
        return wtx.m_amounts[CWalletTx::AVAILABLE_CREDIT].m_value[filter];
    }

    bool allow_used_addresses = (filter & ISMINE_USED) || !wallet.IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE);
    CAmount nCredit = 0;
    Txid hashTx = wtx.GetHash();
    for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
        const CTxOut& txout = wtx.tx->vout[i];
        if (!wallet.IsSpent(COutPoint(hashTx, i)) && (allow_used_addresses || !wallet.IsSpentKey(txout.scriptPubKey))) {
            nCredit += OutputGetCredit(wallet, txout, filter);
            if (!MoneyRange(nCredit))
                throw std::runtime_error(std::string(__func__) + " : value out of range");
        }
    }

    if (allow_cache) {
        wtx.m_amounts[CWalletTx::AVAILABLE_CREDIT].Set(filter, nCredit);
        wtx.m_is_cache_empty = false;
    }

    return nCredit;
}

void CachedTxGetAmounts(const CWallet& wallet, const CWalletTx& wtx,
                  std::list<COutputEntry>& listReceived,
                  std::list<COutputEntry>& listSent, CAmount& nFee, const isminefilter& filter,
                  bool include_change)
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();

    // Compute fee:
    CAmount nDebit = CachedTxGetDebit(wallet, wtx, filter);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        CAmount nFeeOut = wtx.tx->GetFeeOut();
        nFee = (nFeeOut > 0) ? nFeeOut : (nDebit - wtx.tx->GetValueOut());
    }

    LOCK(wallet.cs_wallet);
    // Sent/received.
    for (unsigned int i = 0; i < wtx.tx->vout.size(); ++i)
    {
        const CTxOut& txout = wtx.tx->vout[i];
        isminetype fIsMine = wallet.IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            if (!include_change && OutputIsChange(wallet, txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;

        // Confidential-transaction fee outputs carry an empty scriptPubKey
        // (explicit fee placeholder). They are not spendable and have no
        // address; skip them so GetAmounts neither logs a spurious
        // "Unknown transaction type" nor emits fake sent/received entries.
        if (txout.scriptPubKey.empty())
            continue;

// RCPU CT: prefer the original confidential recipient address
        // (rcpux1...) persisted by the sending wallet in mapValue. The
        // on-chain P2WPKH script only carries the 20-byte HASH160, so
        // extracting the destination from the script would degrade the record
        // to the plain rcpu1q address and lose the blinding pubkey.
        const auto vout_addr_it = wtx.mapValue.find("vout_addr_" + std::to_string(i));
        bool addr_restored = false;
        if (vout_addr_it != wtx.mapValue.end()) {
            std::string decode_err;
            CTxDestination restored = DecodeDestination(vout_addr_it->second, decode_err);
            if (decode_err.empty() && IsValidDestination(restored)) {
                address = restored;
                addr_restored = true;
            }
        }
        if (!addr_restored && !ExtractDestination(txout.scriptPubKey, address) && !txout.scriptPubKey.IsUnspendable())
        {
            wallet.WalletLogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                                    wtx.GetHash().ToString());
            address = CNoDestination();
        }

        CAmount nOutValue;
        uint256 blind;
        if (!UnblindConfidentialOutput(wallet, txout, nOutValue, blind)) {
            nOutValue = 0;
        }
        COutputEntry output = {address, nOutValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }

}

bool CachedTxIsFromMe(const CWallet& wallet, const CWalletTx& wtx, const isminefilter& filter)
{
    return (CachedTxGetDebit(wallet, wtx, filter) > 0);
}

bool CachedTxIsTrusted(const CWallet& wallet, const CWalletTx& wtx, std::set<uint256>& trusted_parents)
{
    AssertLockHeld(wallet.cs_wallet);
    int nDepth = wallet.GetTxDepthInMainChain(wtx);
    if (nDepth >= 1) return true;
    if (nDepth < 0) return false;
    // using wtx's cached debit
    if (!wallet.m_spend_zero_conf_change || !CachedTxIsFromMe(wallet, wtx, ISMINE_ALL)) return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool.
    if (!wtx.InMempool()) return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const CTxIn& txin : wtx.tx->vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = wallet.GetWalletTx(txin.prevout.hash);
        if (parent == nullptr) return false;
        const CTxOut& parentOut = parent->tx->vout[txin.prevout.n];
        // Check that this specific input being spent is trusted
        if (wallet.IsMine(parentOut) != ISMINE_SPENDABLE) return false;
        // If we've already trusted this parent, continue
        if (trusted_parents.count(parent->GetHash())) continue;
        // Recurse to check that the parent is also trusted
        if (!CachedTxIsTrusted(wallet, *parent, trusted_parents)) return false;
        trusted_parents.insert(parent->GetHash());
    }
    return true;
}

bool CachedTxIsTrusted(const CWallet& wallet, const CWalletTx& wtx)
{
    std::set<uint256> trusted_parents;
    LOCK(wallet.cs_wallet);
    return CachedTxIsTrusted(wallet, wtx, trusted_parents);
}

Balance GetBalance(const CWallet& wallet, const int min_depth, bool avoid_reuse)
{
    Balance ret;
    isminefilter reuse_filter = avoid_reuse ? ISMINE_NO : ISMINE_USED;
    {
        LOCK(wallet.cs_wallet);
        std::set<uint256> trusted_parents;
        for (const auto& entry : wallet.mapWallet)
        {
            const CWalletTx& wtx = entry.second;
            const bool is_trusted{CachedTxIsTrusted(wallet, wtx, trusted_parents)};
            const int tx_depth{wallet.GetTxDepthInMainChain(wtx)};
            const CAmount tx_credit_mine{CachedTxGetAvailableCredit(wallet, wtx, ISMINE_SPENDABLE | reuse_filter)};
            const CAmount tx_credit_watchonly{CachedTxGetAvailableCredit(wallet, wtx, ISMINE_WATCH_ONLY | reuse_filter)};
            if (is_trusted && tx_depth >= min_depth) {
                ret.m_mine_trusted += tx_credit_mine;
                ret.m_watchonly_trusted += tx_credit_watchonly;
            }
            if (!is_trusted && tx_depth == 0 && wtx.InMempool()) {
                ret.m_mine_untrusted_pending += tx_credit_mine;
                ret.m_watchonly_untrusted_pending += tx_credit_watchonly;
            }
            ret.m_mine_immature += CachedTxGetImmatureCredit(wallet, wtx, ISMINE_SPENDABLE);
            ret.m_watchonly_immature += CachedTxGetImmatureCredit(wallet, wtx, ISMINE_WATCH_ONLY);
        }
    }
    return ret;
}

std::map<CTxDestination, CAmount> GetAddressBalances(const CWallet& wallet)
{
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK(wallet.cs_wallet);
        std::set<uint256> trusted_parents;
        for (const auto& walletEntry : wallet.mapWallet)
        {
            const CWalletTx& wtx = walletEntry.second;

            if (!CachedTxIsTrusted(wallet, wtx, trusted_parents))
                continue;

            if (wallet.IsTxImmatureCoinBase(wtx))
                continue;

            int nDepth = wallet.GetTxDepthInMainChain(wtx);
            if (nDepth < (CachedTxIsFromMe(wallet, wtx, ISMINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
                const auto& output = wtx.tx->vout[i];
                CTxDestination addr;
                if (!wallet.IsMine(output))
                    continue;
                if(!ExtractDestination(output.scriptPubKey, addr))
                    continue;

                CAmount nVal;
                uint256 blind;
                if (!UnblindConfidentialOutput(wallet, output, nVal, blind)) {
                    nVal = 0;
                }
                CAmount n = wallet.IsSpent(COutPoint(Txid::FromUint256(walletEntry.first), i)) ? 0 : nVal;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

std::set< std::set<CTxDestination> > GetAddressGroupings(const CWallet& wallet)
{
    AssertLockHeld(wallet.cs_wallet);
    std::set< std::set<CTxDestination> > groupings;
    std::set<CTxDestination> grouping;

    for (const auto& walletEntry : wallet.mapWallet)
    {
        const CWalletTx& wtx = walletEntry.second;

        if (wtx.tx->vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            for (const CTxIn& txin : wtx.tx->vin)
            {
                CTxDestination address;
                if(!InputIsMine(wallet, txin)) /* If this input isn't mine, ignore it */
                    continue;
                if(!ExtractDestination(wallet.mapWallet.at(txin.prevout.hash).tx->vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               for (const CTxOut& txout : wtx.tx->vout)
                   if (OutputIsChange(wallet, txout))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (const auto& txout : wtx.tx->vout)
            if (wallet.IsMine(txout))
            {
                CTxDestination address;
                if(!ExtractDestination(txout.scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    std::set< std::set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    std::map< CTxDestination, std::set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    for (const std::set<CTxDestination>& _grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        std::set< std::set<CTxDestination>* > hits;
        std::map< CTxDestination, std::set<CTxDestination>* >::iterator it;
        for (const CTxDestination& address : _grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        std::set<CTxDestination>* merged = new std::set<CTxDestination>(_grouping);
        for (std::set<CTxDestination>* hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (const CTxDestination& element : *merged)
            setmap[element] = merged;
    }

    std::set< std::set<CTxDestination> > ret;
    for (const std::set<CTxDestination>* uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}
} // namespace wallet
