#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test confidential (rcpux1) receive addresses and descriptor backfill.

Exercises the Path-B receive flow end to end:
 1. A fresh descriptor wallet generates a confidential address
    (rrcpux1... on regtest) whose validateaddress reports confidential=true.
 2. A payment sent to that address credits the correct amount after unlock
    (not a zeroed balance).
 3. A wallet that only holds bech32 (84h) descriptors cannot issue
    confidential addresses before it is reloaded.
 4. After reloading, the missing confidential SPK managers are backfilled
    automatically and the same wallet can generate rcpux1 addresses.
 5. A private-keys-disabled wallet keeps failing (nothing to backfill).
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class WalletConfidentialTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Setting up wallets")
        node = self.nodes[0]
        node.createwallet(wallet_name="payer")
        payer = node.get_wallet_rpc("payer")
        node.createwallet(wallet_name="receiver")
        receiver = node.get_wallet_rpc("receiver")

        self.log.info("Mining coins for the payer")
        self.generatetoaddress(node, 101, payer.getnewaddress())

        # ---- 1. Fresh descriptor wallet: confidential receive address ----
        # Encrypt first (like a real user, the wallet lives encrypted): the
        # rebuild inside EncryptWallet also creates confidential SPK managers,
        # so the confidential receive address must work afterwards.
        self.log.info("Encrypting the receiver wallet, then generating a confidential address")
        receiver.encryptwallet("pass")
        receiver.walletpassphrase("pass", 100)
        addr = receiver.getnewaddress("", "confidential")
        assert addr.startswith("rrcpux1"), addr
        info = receiver.validateaddress(addr)
        assert_equal(info["isvalid"], True)
        assert_equal(info["confidential"], True)
        # Non-regression: plain bech32 receive addresses still work.
        bech32_addr = receiver.getnewaddress("", "bech32")
        assert receiver.validateaddress(bech32_addr)["isvalid"]

        # ---- 2. Pay to the confidential address; balance must be exact ----
        self.log.info("Paying 0.8 to the confidential address")
        receiver.walletlock()
        payer.sendtoaddress(addr, Decimal("0.8"))
        self.generatetoaddress(node, 1, payer.getnewaddress())
        # A locked wallet cannot unblind the confidential output, so the
        # balance is still 0; after unlock (Path-B unblind) the credited
        # amount must be exactly 0.8, never a zeroed balance.
        assert_equal(receiver.getbalance(), Decimal("0"))
        receiver.walletpassphrase("pass", 100)
        assert_equal(receiver.getbalance(), Decimal("0.8"))

        # ---- 3. Only-bech32 wallet cannot issue confidential addresses ----
        self.log.info("Building a wallet with only bech32 (84h) descriptors")
        node.createwallet(wallet_name="oldw", disable_private_keys=False, blank=True, descriptors=True)
        oldw = node.get_wallet_rpc("oldw")
        descs = payer.listdescriptors(True)["descriptors"]
        bech32_descs = [d for d in descs if "/84h/" in d["desc"]]
        assert_equal(len(bech32_descs), 2)
        import_reqs = []
        for d in bech32_descs:
            import_reqs.append({
                "desc": d["desc"],
                "timestamp": "now",
                "active": True,
                "internal": d["internal"],
            })
        res = oldw.importdescriptors(import_reqs)
        assert all(r["success"] for r in res), res
        # In-process (before a reload) the wallet has no confidential SPKM and
        # must keep failing exactly like a pre-1.1.3 wallet.
        assert_raises_rpc_error(
            -12,
            "No confidential addresses available",
            oldw.getnewaddress, "", "confidential",
        )

        # ---- 4. Reload backfills the confidential SPK managers ----
        self.log.info("Reloading the wallet: confidential SPK managers must be backfilled")
        self.restart_node(0)
        node.loadwallet("oldw")
        oldw = node.get_wallet_rpc("oldw")
        addr2 = oldw.getnewaddress("", "confidential")
        assert addr2.startswith("rrcpux1"), addr2
        assert_equal(oldw.validateaddress(addr2)["confidential"], True)

        # ---- 5. Disabled-private-keys wallet still fails ----
        self.log.info("A private-keys-disabled wallet keeps failing")
        node.createwallet(wallet_name="watchw", disable_private_keys=True, blank=True, descriptors=True)
        watchw = node.get_wallet_rpc("watchw")
        assert_raises_rpc_error(
            -12,
            "No confidential addresses available",
            watchw.getnewaddress, "", "confidential",
        )


if __name__ == "__main__":
    WalletConfidentialTest().main()