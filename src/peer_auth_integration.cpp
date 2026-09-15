// Copyright (c) 2026 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// !RCPU FIX H-3: P2P Peer Identity Authentication Integration Patch
//
// This file contains the integration code to be added to net_processing.cpp.
// It adds a PEERAUTH message handler between VERSION and VERACK processing.
//
// === Integration steps ===
// 1. Add #include <peer_auth.h> at the top of net_processing.cpp
// 2. Add PeerAuth initialization in PeerManagerImpl constructor
// 3. Add challenge nonce generation in PushNodeVersion()
// 4. Add PEERAUTH message handler in ProcessMessage()
// 5. Add PEERAUTH message sending logic after sending VERACK
// 6. Register the new message type in protocol.h

#include <peer_auth.h>

// --- In protocol.h, add to the list of message types ---
// static constexpr const char* PEERAUTH = "peerauth";
// (Add to NetMsgType namespace)

// --- In PeerManagerImpl class, add members ---
// In private section:
//   uint256 m_local_challenge;  // per-peer challenge we sent
//   uint256 m_remote_challenge; // per-peer challenge we received
//   CPubKey m_peer_identity_pubkey; // peer's identity public key
//   bool m_peer_authenticated = false;

// --- In PushNodeVersion(), after sending VERSION, generate challenge ---
// m_local_challenge = PeerAuth::Instance().GenerateChallenge();

// --- After sending VERACK, send PEERAUTH ---
// {
//     std::vector<unsigned char> sig;
//     int64_t ts = GetTime();
//     if (PeerAuth::Instance().SignChallenge(
//             m_local_challenge, m_remote_challenge, ts, sig)) {
//         MakeAndPushMessage(pfrom, NetMsgType::PEERAUTH,
//             PeerAuth::Instance().GetIdentityPubKey(),
//             m_local_challenge, ts, sig);
//     }
// }

// --- In ProcessMessage(), add handler before the "post-verack" guard ---
// if (msg_type == NetMsgType::PEERAUTH) {
//     if (pfrom.fSuccessfullyConnected) {
//         LogPrint(BCLog::NET, "peerauth received after verack from peer=%d; disconnecting\n",
//                  pfrom.GetId());
//         pfrom.fDisconnect = true;
//         return;
//     }
//
//     CPubKey peer_pubkey;
//     uint256 remote_challenge;
//     int64_t timestamp;
//     std::vector<unsigned char> sig;
//
//     vRecv >> peer_pubkey >> remote_challenge >> timestamp >> sig;
//
//     if (!peer_pubkey.IsValid()) {
//         LogPrint(BCLog::NET, "invalid peerauth pubkey from peer=%d\n", pfrom.GetId());
//         pfrom.fDisconnect = true;
//         return;
//     }
//
//     // The peer signs (their_challenge_to_us || our_challenge_to_them || timestamp).
//     // remote_challenge should match our m_local_challenge.
//     if (!PeerAuth::Instance().VerifyChallenge(
//             peer_pubkey, m_local_challenge, remote_challenge, timestamp, sig)) {
//         LogPrint(BCLog::NET, "peerauth verification failed for peer=%d\n", pfrom.GetId());
//         // Don't disconnect: backward compatibility with peers that don't support PEERAUTH.
//         // But mark as unauthenticated for potential rate-limiting.
//         return;
//     }
//
//     m_peer_identity_pubkey = peer_pubkey;
//     m_peer_authenticated = true;
//     LogPrint(BCLog::NET, "peer=%d authenticated with identity pubkey %s\n",
//              pfrom.GetId(), peer_pubkey.GetID().ToString());
//     return;
// }
