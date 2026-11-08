#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""M-5: a locked/restarted encrypted wallet must not keep plaintext CT amounts.

Before M-5 the wallet persisted ct_unblind_<n> values while unlocked, so a
locked wallet could still report the real balance. That leaked every CT amount
to anyone with a copy of wallet.dat. v1.4.5 removes the plaintext cache: a
locked wallet reports 0 and an empty listunspent until the passphrase is
supplied; unlocking recomputes the values from chain data.
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

        # Encrypting locks the wallet. Unlock once so the wallet can unblind,
        # then lock it again: M-5 deliberately does NOT retain a plaintext
        # cache, so the locked balance must be 0 and listunspent empty.
        ct.encryptwallet("pass")
        ct.walletpassphrase("pass", 100)
        assert_equal(ct.getbalance(), Decimal("0.5"))
        ct.walletlock()
        # M-5 clears the in-memory amount caches on lock/unlock as well as the
        # disk cache, so the locked wallet must not show the previous value.
        assert_equal(ct.getbalance(), Decimal("0"))
        assert_equal(ct.listunspent(), [])

        # A restart must not resurrect the plaintext cache: the wallet is
        # loaded locked and cannot unblind until the passphrase is supplied.
        self.restart_node(0)
        node = self.nodes[0]
        if "ct" not in node.listwallets():
            node.loadwallet("ct")
        if "miner" not in node.listwallets():
            node.loadwallet("miner")
        ct = node.get_wallet_rpc("ct")
        miner = node.get_wallet_rpc("miner")
        assert_equal(ct.getbalance(), Decimal("0"))
        assert_equal(ct.listunspent(), [])

        # Sanity: unlocking recomputes the balance from chain data and
        # spending still works after the restart.
        ct.walletpassphrase("pass", 100)
        assert_equal(ct.getbalance(), Decimal("0.5"))
        assert_equal(ct.listunspent()[0]["amount"], Decimal("0.5"))
        txid = ct.sendtoaddress(miner.getnewaddress("", "bech32"), Decimal("0.4"))
        assert txid
        self.generatetoaddress(node, 1, miner.getnewaddress("", "bech32"))


if __name__ == '__main__':
    WalletCTLockedBalanceTest().main()
