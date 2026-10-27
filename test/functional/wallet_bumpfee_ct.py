#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""bumpfee is unavailable on RCPU chains, so a stuck transaction -- including
a confidential (CT) one -- cannot be silently re-fee'd.

RCPU disables replacement by policy (see feature_rbf_rcpu.py). bumpfee refuses
before it even parses the txid, so it covers every wallet transaction, explicit
or confidential. The feebumper guards that reject confidential inputs/outputs
instead of counting them as 0 are therefore defense-in-depth, ready for the day
replacement is enabled: without them the replacement would be built by a
CreateTransaction path that never blinds anything.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class BumpfeeUnavailableTest(BitcoinTestFramework):
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
        node.createwallet(wallet_name="bump", descriptors=True)
        wallet = node.get_wallet_rpc("bump")

        self.log.info("Mining 120 blocks so coinbase outputs are mature")
        mine_addr = wallet.getnewaddress("", "bech32")
        for _ in range(3):
            self.generatetoaddress(node, 40, mine_addr)
        assert node.getblockcount() >= 120

        self.log.info("bumpfee refuses a payment to a plain rcpu1 address")
        explicit_dest = wallet.getnewaddress("", "bech32")
        explicit_txid = wallet.sendtoaddress(explicit_dest, Decimal("10"))
        decoded_a = node.getrawtransaction(explicit_txid, True)
        payer_outs = [o for o in decoded_a["vout"]
                      if o["scriptPubKey"].get("address") == explicit_dest]
        assert len(payer_outs) == 1, \
            f"expected one explicit output to {explicit_dest}: {decoded_a['vout']}"
        assert_equal(payer_outs[0]["value"], Decimal("10"))
        # resolve_change_key() upgrades wallet-owned change to Path C regardless
        # of the address format, so this is a mixed tx (explicit recipient plus
        # confidential change), not an all-explicit one.
        assert any(o["value"] == "confidential" for o in decoded_a["vout"]), \
            f"expected confidential change: {decoded_a['vout']}"
        assert_raises_rpc_error(-8, "RBF is not supported.", wallet.bumpfee, explicit_txid)

        self.log.info("bumpfee refuses a confidential transaction")
        conf_addr = wallet.getnewaddress("", "confidential")
        ct_txid = wallet.sendtoaddress(conf_addr, Decimal("1"))
        decoded_b = node.getrawtransaction(ct_txid, True)
        assert any(o["value"] == "confidential" for o in decoded_b["vout"]), \
            f"expected a confidential output: {decoded_b['vout']}"
        # A fee computed from 0-valued CT inputs/outputs would be meaningless,
        # but bumpfee never gets that far: replacement is disabled outright.
        assert_raises_rpc_error(-8, "RBF is not supported.", wallet.bumpfee, ct_txid)

        self.log.info("Neither transaction was replaced behind our back")
        mempool = node.getrawmempool()
        assert explicit_txid in mempool, f"{explicit_txid} missing from mempool"
        assert ct_txid in mempool, f"{ct_txid} missing from mempool"


if __name__ == '__main__':
    BumpfeeUnavailableTest().main()
