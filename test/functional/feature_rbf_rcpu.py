#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Replacement (BIP125) is intentionally disabled on every RCPU chain.

RCPU refuses replacement at each entry point. This test pins the gates that do
not need a wallet bump path:

 1. createrawtransaction refuses replaceable=true.
 2. The mempool enforces first-seen: a conflicting spend of an unconfirmed
    output is rejected with "txn-mempool-conflict", even when the original
    transaction was hand-crafted with a BIP125-signalling nSequence.
 3. -mempoolfullrbf aborts startup.
 4. -walletrbf aborts startup.

wallet_bumpfee_ct.py covers the remaining gate, the bumpfee RPC.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal, assert_raises_rpc_error


class RbfPolicyTest(BitcoinTestFramework):
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
        node.createwallet(wallet_name="policy", descriptors=True)
        wallet = node.get_wallet_rpc("policy")

        self.log.info("Mining 120 blocks so coinbase outputs are mature")
        mine_addr = wallet.getnewaddress("", "bech32")
        for _ in range(3):
            self.generatetoaddress(node, 40, mine_addr)
        assert node.getblockcount() >= 120

        utxo = wallet.listunspent()[0]
        prevout = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
        dest = wallet.getnewaddress("", "bech32")

        self.log.info("createrawtransaction refuses BIP125 signalling")
        assert_raises_rpc_error(
            -8, "RBF is not supported.",
            node.createrawtransaction,
            prevout, {dest: utxo["amount"] - Decimal("0.0001")}, 0, True,
        )

        self.log.info("Mempool still enforces first-seen for conflicting spends")
        # Signal BIP125 by hand: the input object accepts an explicit sequence,
        # which is the only route left once replaceable=true is refused.
        signalling_prevout = [{"txid": utxo["txid"], "vout": utxo["vout"], "sequence": 0xfffffffd}]
        raw_a = node.createrawtransaction(signalling_prevout, {dest: utxo["amount"] - Decimal("0.0001")})
        signed_a = wallet.signrawtransactionwithwallet(raw_a)
        assert_equal(signed_a["complete"], True)
        tx_a = node.sendrawtransaction(signed_a["hex"])
        assert_equal(node.getrawtransaction(tx_a, True)["vin"][0]["sequence"], 0xfffffffd)

        dest_b = wallet.getnewaddress("", "bech32")
        raw_b = node.createrawtransaction(
            signalling_prevout, {dest_b: utxo["amount"] - Decimal("0.002")})
        signed_b = wallet.signrawtransactionwithwallet(raw_b)
        assert_equal(signed_b["complete"], True)
        assert_raises_rpc_error(
            -26, "txn-mempool-conflict",
            node.sendrawtransaction, signed_b["hex"],
        )
        assert tx_a in node.getrawmempool(), "first-seen transaction was dropped"

        self.log.info("-mempoolfullrbf and -walletrbf abort startup")
        self.stop_node(0)
        node.assert_start_raises_init_error(
            ["-mempoolfullrbf=1"], "RBF is not supported\\.", match=ErrorMatch.PARTIAL_REGEX)
        self.start_node(0)
        self.stop_node(0)
        node.assert_start_raises_init_error(
            ["-walletrbf=1"], "RBF is not supported\\.", match=ErrorMatch.PARTIAL_REGEX)
        self.start_node(0)


if __name__ == '__main__':
    RbfPolicyTest().main()
