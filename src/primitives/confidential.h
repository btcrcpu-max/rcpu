// Copyright (c) 2015 Gregory Maxwell
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_CONFIDENTIAL_H
#define BITCOIN_PRIMITIVES_CONFIDENTIAL_H

#include <consensus/amount.h>
#include <crypto/common.h>
#include <serialize.h>
#include <span.h>
#include <util/strencodings.h>

#include <vector>

/** Global flag: confidential-transaction mode (Elements-style g_con_elementsmode). */
extern bool g_con_elementsmode;

/** Thread-local flag: whether the transaction currently being (de)serialized is in CT format. */
extern thread_local bool g_ct_serialization;

/**
 * Confidential values and nonces share enough code in common that it makes
 * sense to define a common abstract base class.
 *
 * Serialization layout:
 *   byte 0            : version/prefix
 *     0x00            : null (no data follows)
 *     0x01            : explicit value (nExplicitSize-1 bytes follow)
 *     PrefixA/PrefixB : committed value (33-byte compressed EC point follows)
 */
template<size_t ExplicitSize, unsigned char PrefixA, unsigned char PrefixB>
class CConfidentialCommitment
{
public:
    static const size_t nExplicitSize = ExplicitSize;
    static const size_t nCommittedSize = 33;

    std::vector<unsigned char> vchCommitment;

    CConfidentialCommitment() { SetNull(); }

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        unsigned char version = vchCommitment.empty()? 0: vchCommitment[0];
        s << version;
        if (vchCommitment.size() > 1) {
            for (size_t i = 0; i < vchCommitment.size() - 1; i++) {
                s << vchCommitment[i + 1];
            }
        }
    }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
        unsigned char version = vchCommitment.empty()? 0: vchCommitment[0];
        s >> version;
        switch (version) {
            /* Null */
            case 0:
                vchCommitment.clear();
                return;
            /* Explicit value */
            case 1:
                vchCommitment.resize(nExplicitSize);
                break;
            /* Confidential commitment */
            case PrefixA:
            case PrefixB:
                vchCommitment.resize(nCommittedSize);
                break;
            /* Invalid serialization! */
            default:
                throw std::ios_base::failure("Unrecognized serialization prefix");
        }
        vchCommitment[0] = version;
        if (vchCommitment.size() > 1) {
            s >> Span<unsigned char>(vchCommitment.data() + 1, vchCommitment.size()-1);
        }
    }

    /* Null is the default state when no explicit or confidential value has
     * been set. */
    bool IsNull() const { return vchCommitment.empty(); }
    void SetNull() { vchCommitment.clear(); }

    bool IsExplicit() const
    {
        return vchCommitment.size()==nExplicitSize && vchCommitment[0]==1;
    }

    bool IsCommitment() const
    {
        return vchCommitment.size()==nCommittedSize && (vchCommitment[0]==PrefixA || vchCommitment[0]==PrefixB);
    }

    bool IsValid() const
    {
        return IsNull() || IsExplicit() || IsCommitment();
    }

    std::string GetHex() const { return HexStr(MakeByteSpan(vchCommitment)); }

    friend bool operator==(const CConfidentialCommitment& a, const CConfidentialCommitment& b)
    {
        return a.vchCommitment == b.vchCommitment;
    }

    friend bool operator!=(const CConfidentialCommitment& a, const CConfidentialCommitment& b)
    {
        return !(a == b);
    }
};

/** A 33-byte commitment to a confidential value, or a 64-bit explicit value. */
class CConfidentialValue : public CConfidentialCommitment<9, 8, 9>
{
public:
    CConfidentialValue() { SetNull(); }
    CConfidentialValue(CAmount nAmount) { SetToAmount(nAmount); }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
        CConfidentialCommitment::Unserialize(s);
    }

    /* An explicit value is called an amount. The first byte indicates it is
     * an explicit value, and the remaining 8 bytes is the value serialized as
     * a 64-bit big-endian integer. */
    CAmount GetAmount() const
    {
        if (!IsExplicit()) return 0;
        return ReadBE64(&vchCommitment[1]);
    }
    void SetToAmount(CAmount nAmount);
};

/**
 * A 33-byte data field that typically is used to convey to the
 * recipient the ECDH ephemeral key (an EC point) for deriving the
 * transaction output blinding factor. */
class CConfidentialNonce : public CConfidentialCommitment<33, 2, 3>
{
public:
    CConfidentialNonce() { SetNull(); }
};

#endif // BITCOIN_PRIMITIVES_CONFIDENTIAL_H
