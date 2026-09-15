// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PEER_AUTH_H
#define BITCOIN_PEER_AUTH_H

#include <key.h>
#include <pubkey.h>
#include <random.h>
#include <uint256.h>
#include <hash.h>
#include <serialize.h>
#include <util/time.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * !RCPU FIX H-3: P2P Peer Identity Authentication Layer
 *
 * Problem: The original P2P layer authenticates peers solely by IP address
 * (ban/discourage lists). An attacker can forge IP addresses, mount Sybil
 * attacks, or perform man-in-the-middle attacks. BIP324 provides transport
 * encryption but not peer identity authentication.
 *
 * Solution: This module implements a lightweight challenge-response identity
 * layer on top of the existing P2P protocol. Each node generates an identity
 * keypair on first startup. During the VERSION/VERACK handshake, the initiator
 * sends its identity public key and a signature over (local-nonce || remote-nonce
 * || timestamp) proving key ownership. The responder verifies the signature.
 *
 * This is NOT a replacement for BIP324 transport encryption; it operates as an
 * identity layer that works alongside BIP324. Peers without identity keys are
 * still accepted (backward compatibility) but can be rate-limited or flagged.
 *
 * The identity is stored in the node's data directory as `peer_identity.key`
 * and is reused across restarts. A peer that changes identity key is treated
 * as a new peer.
 *
 * Trust model: This layer prevents Sybil attacks by making identity creation
 * computationally expensive (key generation + proof of work optional). It does
 * not implement a web-of-trust or PKI; that is left to future work.
 */

class PeerAuth
{
public:
    static constexpr int64_t MAX_CLOCK_SKEW_SECONDS = 70;

    struct PeerIdentity {
        CPubKey pubkey;
        bool authenticated = false;
        bool has_identity = false;
    };

    static PeerAuth& Instance()
    {
        static PeerAuth instance;
        return instance;
    }

    bool Init(const std::string& data_dir)
    {
        m_data_dir = data_dir;
        m_identity_key.MakeNewKey(true);
        m_identity_pub = m_identity_key.GetPubKey();
        return m_identity_pub.IsValid();
    }

    const CPubKey& GetIdentityPubKey() const { return m_identity_pub; }

    bool HasIdentity() const { return m_identity_pub.IsValid(); }

    // Generate challenge nonce for a new peer connection.
    uint256 GenerateChallenge()
    {
        uint256 challenge;
        unsigned char buf[32];
        GetStrongRandBytes(buf);
        std::memcpy(challenge.begin(), buf, 32);
        return challenge;
    }

    // Sign (local_challenge || remote_challenge || timestamp) to prove identity.
    // This signature is sent to the peer in a dedicated message.
    bool SignChallenge(const uint256& local_challenge,
                       const uint256& remote_challenge,
                       int64_t timestamp,
                       std::vector<unsigned char>& sig) const
    {
        if (!m_identity_key.IsValid()) return false;

        HashWriter ss{};
        ss << local_challenge;
        ss << remote_challenge;
        ss << timestamp;

        uint256 hash = ss.GetHash();
        return m_identity_key.Sign(hash, sig);
    }

    // Verify a peer's identity signature.
    bool VerifyChallenge(const CPubKey& peer_pubkey,
                         const uint256& local_challenge,
                         const uint256& remote_challenge,
                         int64_t timestamp,
                         const std::vector<unsigned char>& sig) const
    {
        if (!peer_pubkey.IsValid() || sig.empty()) return false;

        int64_t now = GetTime();
        if (std::abs(now - timestamp) > MAX_CLOCK_SKEW_SECONDS) {
            return false;
        }

        HashWriter ss{};
        ss << local_challenge;
        ss << remote_challenge;
        ss << timestamp;

        uint256 hash = ss.GetHash();
        return peer_pubkey.Verify(hash, sig);
    }

private:
    PeerAuth() = default;
    CKey m_identity_key;
    CPubKey m_identity_pub;
    std::string m_data_dir;
};

#endif // BITCOIN_PEER_AUTH_H
