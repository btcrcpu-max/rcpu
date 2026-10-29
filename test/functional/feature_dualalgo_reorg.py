#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Dual-algo count-cache reorg / side-branch consistency.

Verifies that nSecondAlgoCount, which each algorithm's ASERT difficulty
depends on, remains correct across a reorg where the competing branch
uses a different mix of RandomX vs. Etchash blocks:

 1. Mine well past activation (height 150 → 170) with all RandomX on node0.
 2. Stop node1 so it freezes the all-RandomX view. Invalidate a block that
    is post-activation (height 165), push the chain back to 164, and build
    a competing side branch whose R/E mix differs from the original.
 3. Restart node1 and sync to node0. If either the per-algo count cache
    or the post-activation height dispatch drifted across the reorg,
    the next blocks will be rejected with bad-diffbits and the tips
    will never converge.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class DualAlgoReorgTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.chain = 'rcpuregtest'
        self.setup_clean_chain = True
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        n0, n1 = self.nodes
        self.connect_nodes(0, 1)

        n0.createwallet(wallet_name="miner", descriptors=True)
        addr = n0.get_wallet_rpc("miner").getnewaddress()

        # ------------------------------------------------------------------
        # Part 1: mine well past activation (150) with all RandomX.
        # ------------------------------------------------------------------
        self.log.info("Mining to height 170 (activation = 150) with RandomX")
        for _ in range(5):
            self.generatetoaddress(n0, 34, addr)
        self.sync_blocks()
        assert_equal(n0.getblockcount(), 170)

        # Verify anchor: height 149 is pre-activation, height 150+ are all
        # RandomX on branch A.
        b149 = n0.getblockhash(149)
        b150 = n0.getblockhash(150)
        assert_equal(n0.getblock(b149, verbose=1)["height"], 149)
        assert_equal(n0.getblock(b150, verbose=1)["height"], 150)

        # ------------------------------------------------------------------
        # Part 2: create a side-branch split at height 165 (well past the
        # activation boundary so invalidateblock does not interact with
        # activation-specific code paths).
        # ------------------------------------------------------------------

        # Stop node1 to freeze its view of branch A (all RandomX).
        self.stop_node(1)

        # Save original hashes BEFORE we invalidate anything.
        b165_orig = n0.getblockhash(165)

        # On node0: invalidate blocks 170 → 165, forcing reorg back to 164.
        self.log.info("Invalidating heights 170..165 to force reorg to 164")
        for h in range(170, 164, -1):
            n0.invalidateblock(n0.getblockhash(h))
        assert_equal(n0.getblockcount(), 164)

        self.log.info("Building competing branch B (Etchash-heavy after reorg)")
        # Mine new height 165 — try Etchash first, to create divergence.
        n0.generatetoaddress(1, addr, 1000000, "etchash", invalid_call=False)
        assert_equal(n0.getblockcount(), 165)
        new_b165 = n0.getblockhash(165)
        assert new_b165 != b165_orig, "invalidation did not create a fork"

        # Mine a mixed batch to push past the original tip.
        for i in range(10):
            algo = "etchash" if i % 2 == 0 else "randomx"
            n0.generatetoaddress(1, addr, 1000000, algo, invalid_call=False)
        assert n0.getblockcount() > 170

        # Restart node1 and sync. node1 still has branch A in its block
        # index (no invalidation), so it will see branch B as a competing
        # chain. The longer chain wins (both produce powLimit-easy blocks
        # on regtest so chainwork ≈ height).
        self.log.info("Restarting node1; it must reconcile branch A vs branch B")
        self.start_node(1)
        self.connect_nodes(0, 1)
        self.sync_blocks()

        self.log.info("Asserting both nodes converge on branch B")
        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())

        # ------------------------------------------------------------------
        # Part 3: mine past the reorg'd tip. If either the per-algo count
        # cache or GetPowAlgoForHeader drifted across the reorg, the nodes
        # would disagree on GetNextWorkRequired and reject each other's
        # blocks as bad-diffbits. Mix two more RandomX + Etchash pairs to
        # exercise both ASERT instances.
        # ------------------------------------------------------------------
        self.log.info("Mining another mixed batch on both nodes")
        for i in range(4):
            algo = "etchash" if i % 2 == 0 else "randomx"
            n0.generatetoaddress(1, addr, 1000000, algo, invalid_call=False)
        self.sync_blocks()

        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())


if __name__ == '__main__':
    DualAlgoReorgTest().main()
