#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""P2-4: a locked/restarted wallet must not report a confidential balance as 0.

While the wallet is unlocked it persists the plaintext value of every
confidential output it can unblind (ct_unblind_<n> in the wallet transaction
mapValue). Locking/restarting the wallet then values those outputs from the
cache instead of returning 0 from the failed unblind.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class WalletCTLockedBalanceTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.chain = 'rcpuregtest'
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(wallet_name="miner", descriptors=True)
        miner = node.get_wallet_rpc("miner")
        node.createwallet(wallet_name="ct", descriptors=True)
        ct = node.get_wallet_rpc("ct")

        ct_addr = ct.getnewaddress("", "confidential")
        self.generatetoaddress(node, 101, miner.getnewaddress("", "bech32"))
        miner.sendtoaddress(ct_addr, Decimal("0.5"))
        self.generatetoaddress(node, 1, miner.getnewaddress("", "bech32"))
        assert_equal(ct.getbalance(), Decimal("0.5"))

        # Encrypting locks the wallet. Unlock once so the wallet can persist
        # the unblinded amount, then lock it again and verify the cached value.
        ct.encryptwallet("pass")
        ct.walletpassphrase("pass", 100)
        assert_equal(ct.getbalance(), Decimal("0.5"))
        ct.walletlock()
        assert_equal(ct.getbalance(), Decimal("0.5"))
        assert_equal(ct.listunspent()[0]["amount"], Decimal("0.5"))

        # A restart must not lose the cache: the wallet is loaded locked and
        # cannot unblind until the passphrase is supplied.
        self.restart_node(0)
        node = self.nodes[0]
        if "ct" not in node.listwallets():
            node.loadwallet("ct")
        if "miner" not in node.listwallets():
            node.loadwallet("miner")
        ct = node.get_wallet_rpc("ct")
        miner = node.get_wallet_rpc("miner")
        assert_equal(ct.getbalance(), Decimal("0.5"))
        assert_equal(ct.listunspent()[0]["amount"], Decimal("0.5"))

        # Sanity: unlocking and spending still works after the restart.
        ct.walletpassphrase("pass", 100)
        txid = ct.sendtoaddress(miner.getnewaddress("", "bech32"), Decimal("0.4"))
        assert txid
        self.generatetoaddress(node, 1, miner.getnewaddress("", "bech32"))


if __name__ == '__main__':
    WalletCTLockedBalanceTest().main()
