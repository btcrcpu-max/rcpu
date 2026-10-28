// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2024 The RCPU developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>

#include <chrono>
#include <limits>
#include <map>
#include <vector>

// !RCPU
#include <optional>
// !RCPU END

namespace Consensus {

/**
 * A buried deployment is one where the height of the activation has been hardcoded into
 * the client implementation long after the consensus change has activated. See BIP 90.
 */
enum BuriedDeployment : int16_t {
    // buried deployments get negative values to avoid overlap with DeploymentPos
    DEPLOYMENT_HEIGHTINCB = std::numeric_limits<int16_t>::min(),
    DEPLOYMENT_CLTV,
    DEPLOYMENT_DERSIG,
    DEPLOYMENT_CSV,
    DEPLOYMENT_SEGWIT,
};
constexpr bool ValidDeployment(BuriedDeployment dep) { return dep <= DEPLOYMENT_SEGWIT; }

enum DeploymentPos : uint16_t {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_TAPROOT, // Deployment of Schnorr/Taproot (BIPs 340-342)
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in deploymentinfo.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};
constexpr bool ValidDeployment(DeploymentPos dep) { return dep < MAX_VERSION_BITS_DEPLOYMENTS; }

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit{28};
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime{NEVER_ACTIVE};
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout{NEVER_ACTIVE};
    /** If lock in occurs, delay activation until at least this block
     *  height.  Note that activation will only occur on a retarget
     *  boundary.
     */
    int min_activation_height{0};

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;

    /** Special value for nStartTime indicating that the deployment is never active.
     *  This is useful for integrating the code changes for a new feature
     *  prior to deploying it on some or all networks. */
    static constexpr int64_t NEVER_ACTIVE = -2;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /**
     * Hashes of blocks that
     * - are known to be consensus valid, and
     * - buried in the chain, and
     * - fail if the default script verify flags are applied.
     */
    std::map<uint256, uint32_t> script_flag_exceptions;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Don't warn about unknown BIP 9 activations below this height.
     * This prevents us from warning about the CSV and segwit activations. */
    int MinBIP9WarningHeight;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    std::chrono::seconds PowTargetSpacing() const
    {
        return std::chrono::seconds{nPowTargetSpacing};
    }
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    /** The best chain should have at least this much work */
    uint256 nMinimumChainWork;
    /** By default assume that the signatures in ancestors of this block are valid */
    uint256 defaultAssumeValid;

    /**
     * If true, witness commitments contain a payload equal to a Bitcoin Script solution
     * to the signet challenge. See BIP325.
     */
    bool signet_blocks{false};
    std::vector<uint8_t> signet_challenge;

    int DeploymentHeight(BuriedDeployment dep) const
    {
        switch (dep) {
        case DEPLOYMENT_HEIGHTINCB:
            return BIP34Height;
        case DEPLOYMENT_CLTV:
            return BIP65Height;
        case DEPLOYMENT_DERSIG:
            return BIP66Height;
        case DEPLOYMENT_CSV:
            return CSVHeight;
        case DEPLOYMENT_SEGWIT:
            return SegwitHeight;
        } // no default case, so the compiler can warn about missing cases
        return std::numeric_limits<int>::max();
    }

// !RCPU
    bool fPowRandomX{false};
    // P-03: default-initialized so that a chainparams entry that forgets to
    // set it fails loudly (epoch 0) instead of reading uninitialized memory.
    uint32_t nRandomXEpochDuration{0};

    // !RCPU (dual-algo)
    // Height at which dual-algo free competition (RandomX + a second
    // algorithm) activates. Blocks at height >= this value may set
    // BLOCK_VERSION_QHASH (bit 30) in nVersion and are then verified with the
    // second algorithm (Etchash when fDualAlgoEtchash is true, legacy qhash
    // otherwise) against its difficulty state; blocks without the bit are
    // verified with RandomX against the RandomX difficulty state. At height <
    // this value the bit is completely ignored (RandomX-only rules, identical
    // to the pre-fork code paths). The default (INT_MAX) means "never active"
    // for chains that do not opt in.
    int nDualAlgoActivationHeight{std::numeric_limits<int>::max()};

    // Post-activation target spacing for EACH algorithm (seconds). Both
    // algorithms adjust their difficulty independently (per-algo ASERT
    // anchored at the activation height) so each converges to one block per
    // this interval and the combined chain runs at roughly half of it.
    int64_t nDualAlgoTargetSpacing{10 * 60};

    // !RCPU (dual-algo etchash)
    // When true, the second algorithm selected by BLOCK_VERSION_QHASH (bit 30)
    // is Etchash (ECIP-1099 light verification) instead of qhash. The bit-30
    // semantics and the per-algo difficulty machinery are unchanged; this flag
    // only swaps which PoW function validates a bit-30 block at/after
    // nDualAlgoActivationHeight. False keeps the legacy qhash path as a
    // fallback. The bit stays ignored below the activation height either way.
    bool fDualAlgoEtchash{false};

    // Blocks per etchash epoch. epoch = (height - nDualAlgoActivationHeight)
    // / nEtchashEpochLength; the seed is keccak256^epoch(32 zero bytes) and
    // the light-verification cache is derived from that seed. Mining pools
    // MUST take the epoch/seedhash from getblocktemplate and never recompute
    // them locally from an assumed activation height.
    int nEtchashEpochLength{30000};

    // RCPU P2-7: Etchash result byte order. Before this height the historical
    // RCPU implementation interpreted the 32-byte hashimoto result as a
    // little-endian number; standard geth/ethminer treat it as big-endian.
    // Mainnet/testnet set this to nDualAlgoActivationHeight (Etchash is not
    // active before it), so external Etchash miners interoperate from the
    // first Etchash block. std::numeric_limits<int>::max() keeps the legacy
    // behavior (used by unit tests that do not opt in).
    int nEtchashBigEndianHeight{std::numeric_limits<int>::max()};
    // !RCPU (dual-algo) END

/** RCPU: block height at which confidential transactions (tx version == CT_VERSION) activate. */
    int nCTActivationHeight{0};

    /**
     * RCPU: block height at which the legacy (path A, plaintext-nonce) CT
     * nonce encoding is banned. From this height onward, any confidential
     * output whose nonce field is a 33-byte commitment starting with 0x02 is
     * rejected with "bad-ct-legacy-nonce". Path A stores the rewind nonce
     * on-chain, so it leaks both amount and blinding factor; only path B
     * (0x03 || compressed pubkey) is confidential. Set to
     * std::numeric_limits<int>::max() (= never) on networks that must keep
     * path A (e.g. regtest).
     */
    int nBanPathAHeight{std::numeric_limits<int>::max()};

    /**
     * RCPU: block height from which the canonical 112-byte hash domain
     * (serialized header including hashRandomX) is used for RandomX inputs and
     * block-header hashing. The canonical serialization is byte-identical to
     * the legacy raw-memory layout (asserted in primitives/block.h), so the
     * activation does not change any hash value on already-live chains; this
     * parameter is the versioning guard if the header layout ever grows
     */
    /** Used by the ASERT DAA */
    int nASERTActivationHeight; // Block height at which ASERT DAA becomes active, if asertAnchorParams is set.
    int64_t nASERTHalfLife;
    struct ASERTAnchor {
        int nHeight;
        uint32_t nBits;
        int64_t nPrevBlockTime;
    };
    std::optional<ASERTAnchor> asertAnchorParams;

    // !RCPU END
};

} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
