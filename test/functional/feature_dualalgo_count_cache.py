#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Dual-algo count-cache integration test.

Exercises the real BlockManager paths (not just synthetic CBlockIndex
objects) around the dual-algo activation height on rcpuregtest (150):

 1. Mine past activation with RandomX, then mine a mixed RandomX/Etchash
    sequence so nSecondAlgoCount becomes non-zero.
 2. Restart the validating node. LoadBlockIndex() must rebuild the cached
    counts; if the rebuild disagreed with the incremental maintenance in
    AddToBlockIndex(), the next blocks from node0 would be rejected with
    bad-diffbits and the tips would diverge.
 3. Mine another mixed batch and assert both nodes converge.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class DualAlgoCountCacheTest(BitcoinTestFramework):
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

        self.log.info("Mining past the dual-algo activation height (150)")
        # Mine in chunks so a single RPC does not hit the client timeout while
        # RandomX is solving at the easy regtest target.
        for _ in range(4):
            self.generatetoaddress(n0, 40, addr)
        self.sync_blocks()
        assert n0.getblockcount() >= 160

        self.log.info("Mining a mixed RandomX/Etchash sequence")
        for i in range(8):
            algo = "etchash" if i % 2 == 0 else "randomx"
            n0.generatetoaddress(1, addr, 1000000, algo, invalid_call=False)
        self.sync_blocks()
        height_before = n0.getblockcount()

        self.log.info("Restarting node1 to force LoadBlockIndex() rebuild")
        self.restart_node(1)
        self.connect_nodes(0, 1)

        self.log.info("Mining more mixed blocks; node1 must accept them")
        for i in range(8):
            algo = "etchash" if i % 2 == 0 else "randomx"
            n0.generatetoaddress(1, addr, 1000000, algo, invalid_call=False)
        self.sync_blocks()

        assert n0.getblockcount() > height_before
        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())


if __name__ == '__main__':
    DualAlgoCountCacheTest().main()

