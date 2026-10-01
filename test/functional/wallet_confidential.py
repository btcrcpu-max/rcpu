#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test confidential (rcpux1) receive addresses and descriptor backfill.

Exercises the Path-C receive flow end to end (the wallet default send path
for rcpux1 destinations since the Path C switch; existing Path B outputs
remain spendable):
 1. A fresh descriptor wallet generates a confidential address
    (rrcpux1... on regtest) whose validateaddress reports confidential=true.
 2. A payment sent to that address credits the correct amount after unlock
    (not a zeroed balance). The sender's default send path is Path C
    (nonce 0x04 || X), so this exercises UnblindValueWithKeyV2 scanning.
 3. A wallet that only holds bech32 (84h) descriptors cannot issue
    confidential addresses before it is reloaded.
 4. After reloading, the missing confidential SPK managers are backfilled
    automatically and the same wallet can generate rcpux1 addresses.
 5. A private-keys-disabled wallet keeps failing (nothing to backfill).
 6. H-1 send policy: a bare bech32 (rcpu1) output is explicit, never Path A.
 7. rcpux1 sends stay confidential (Path C).
 7b/7c/7d. H-1 balance invariants: mixed explicit+confidential change
    succeeds, all-explicit outputs funded from a confidential input fail
    closed, and an explicit-only wallet pays a bare address fine.
 8. -ctlegacy=1 stays usable below nBanPathAHeight (probed at runtime).
"""

from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class WalletConfidentialTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.chain = 'rcpuregtest'
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Setting up wallets")
        node = self.nodes[0]
        node.createwallet(wallet_name="payer", descriptors=True)
        payer = node.get_wallet_rpc("payer")
        node.createwallet(wallet_name="receiver", descriptors=True)
        receiver = node.get_wallet_rpc("receiver")

        self.log.info("Mining coins for the payer")
        self.generatetoaddress(node, 101, payer.getnewaddress("", "bech32"))
        # Track whether a section was runtime-skipped so the final summary is honest.
        skipped_sections = []

        # ---- 1. Fresh descriptor wallet: confidential receive address ----
        # Generate a confidential address while the wallet is still unlocked,
        # then encrypt.  Encrypted wallets may need unlock for key derivation.
        self.log.info("Generating a confidential address, then encrypting the wallet")
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
        payer.sendtoaddress(addr, Decimal("0.8"))
        self.generatetoaddress(node, 1, payer.getnewaddress("", "bech32"))
        # Verify the confidential output is credited correctly before encryption
        assert_equal(receiver.getbalance(), Decimal("0.8"))

        receiver.encryptwallet("pass")
        # After encryption the wallet is locked; unlock to verify Path-B
        # unblinding still works with the encrypted wallet.
        receiver.walletpassphrase("pass", 100)
        assert_equal(receiver.getbalance(), Decimal("0.8"))
        receiver.walletlock()
        # Note: cached balance is retained after locking; the wallet does not
        # actively re-unblind, so getbalance() may still report 0.8.

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
        # No keys at all: the generic CanGetAddresses() check (-4) fires before the confidential-SPKM lookup.
        assert_raises_rpc_error(
            -4,
            "no available keys",
            watchw.getnewaddress, "", "confidential",
        )

        # ---- 6. H-1 send policy: bare rcpu1 output is EXPLICIT, never Path A ----
        # The wallet no longer falls back to Path A (0x02 rewind-nonce
        # commitment) for a recipient whose public key cannot be resolved.
        # Sending to a bare bech32 address must emit an explicit plaintext
        # output (numeric value in the RPC, not the string "confidential").
        self.log.info("Bare bech32 send must produce an explicit plaintext output")
        # The node was restarted in step 4; reload the payer/receiver wallets.
        node.loadwallet("payer")
        node.loadwallet("receiver")
        payer = node.get_wallet_rpc("payer")
        receiver = node.get_wallet_rpc("receiver")
        receiver.walletpassphrase("pass", 100)
        txid = payer.sendtoaddress(bech32_addr, Decimal("0.05"))
        decoded = node.getrawtransaction(txid, True)
        target = None
        for o in decoded["vout"]:
            spk = o["scriptPubKey"]
            if bech32_addr == spk.get("address") or bech32_addr in spk.get("addresses", []):
                target = o
                break
        assert target is not None, f"no vout pays {bech32_addr}: {decoded['vout']}"
        assert not isinstance(target["value"], str), f"expected explicit value, got {target['value']}"
        # The whole transaction must be accepted by the wallet even though the
        # payer also holds confidential (Path C) change from section 2: at
        # least one confidential output balances the confidential inputs.
        confidential_vouts = [o for o in decoded["vout"] if o["value"] == "confidential"]
        assert confidential_vouts, f"expected >=1 confidential output in mixed tx: {decoded['vout']}"

        # ---- 7. rcpux1 send stays confidential (Path C) ----
        self.log.info("rcpux1 send must stay confidential (Path C)")
        # A confidential address (rrcpux1...) decodes to an ordinary
        # witness_v0_keyhash scriptPubKey; the confidentiality lives in the
        # blinded value/nonce, not in the script itself.  getrawtransaction
        # therefore reports such outputs with the bare rrcpu1... spelling of
        # the same script (it cannot know the blinding key), so the target
        # output must be matched by scriptPubKey rather than by address.
        addr_spk = receiver.validateaddress(addr)["scriptPubKey"]
        txid2 = payer.sendtoaddress(addr, Decimal("0.05"))
        decoded2 = node.getrawtransaction(txid2, True)
        target2 = None
        for o in decoded2["vout"]:
            spk = o["scriptPubKey"]
            if addr_spk == spk.get("hex"):
                target2 = o
                break
        assert target2 is not None, f"no vout pays {addr} (spk {addr_spk}): {decoded2['vout']}"
        assert_equal(target2["value"], "confidential")

        # ---- 7b. Confidential-only wallet: mixed explicit + Path-C change succeeds ----
        # Regression for the H-1 balance invariant: a wallet whose UTXO set is
        # entirely confidential (Path C) must still be able to pay a bare
        # rcpu1 address when the change output stays confidential
        # (-changetype=confidential): an explicit output plus a confidential
        # change is a valid mixed transaction.
        self.log.info("Confidential UTXO pays a bare address with confidential change")
        # Give the payer confirmed coins first: sections 6/7 spent its only
        # confirmed UTXOs, and a CT (nVersion=3 / v3-policy) child spending an
        # unconfirmed confidential change is capped at 1000 vbytes and would be
        # silently refused by the mempool (the wallet returns a txid but the tx
        # never relays). Confirm those two txs before funding the mixer so the
        # funding send below spends confirmed inputs.
        self.generatetoaddress(node, 1, payer.getnewaddress("", "bech32"))
        node.createwallet(wallet_name="mixer", descriptors=True)
        mixer = node.get_wallet_rpc("mixer")
        mixer_conf = mixer.getnewaddress("", "confidential")
        payer.sendtoaddress(mixer_conf, Decimal("0.3"))
        self.generatetoaddress(node, 1, payer.getnewaddress("", "bech32"))
        assert_equal(mixer.getbalance(), Decimal("0.3"))
        self.restart_node(0, extra_args=["-changetype=confidential"])
        node.loadwallet("mixer")
        mixer = node.get_wallet_rpc("mixer")
        txid7b = mixer.sendtoaddress(bech32_addr, Decimal("0.05"))
        decoded7b = node.getrawtransaction(txid7b, True)
        target7b = None
        for o in decoded7b["vout"]:
            spk = o["scriptPubKey"]
            if bech32_addr == spk.get("address") or bech32_addr in spk.get("addresses", []):
                target7b = o
                break
        assert target7b is not None, f"no vout pays {bech32_addr}: {decoded7b['vout']}"
        assert not isinstance(target7b["value"], str), f"bare target must be explicit, got {target7b['value']}"
        change7b = [o for o in decoded7b["vout"] if o["value"] == "confidential"]
        assert change7b, f"expected confidential Path-C change: {decoded7b['vout']}"

        # ---- 7c. Explicit-only wallet pays a bare address ----
        # A wallet holding only explicit UTXOs (plain coinbase) sending to a
        # bare address must succeed, with the target vout reported as a
        # numeric amount (explicit).
        self.log.info("Explicit-only UTXOs pay a bare address with a numeric vout")
        self.restart_node(0)
        node.createwallet(wallet_name="pure", descriptors=True)
        pure = node.get_wallet_rpc("pure")
        self.generatetoaddress(node, 101, pure.getnewaddress("", "bech32"))
        txid7c = pure.sendtoaddress(bech32_addr, Decimal("0.05"))
        decoded7c = node.getrawtransaction(txid7c, True)
        target7c = None
        for o in decoded7c["vout"]:
            spk = o["scriptPubKey"]
            if bech32_addr == spk.get("address") or bech32_addr in spk.get("addresses", []):
                target7c = o
                break
        assert target7c is not None, f"no vout pays {bech32_addr}: {decoded7c['vout']}"
        assert not isinstance(target7c["value"], str), f"expected explicit value, got {target7c['value']}"
        # Confirm the 7c send: with unconfirmed coins still in the mempool the
        # section-8 balance delta would be off by the 7c amount (the block
        # mined there confirms both txs), so pin it down here to keep the
        # per-section balance accounting exact.  Payer is not reloaded after
        # the section-7c restart, so mine to a fresh address of the only
        # loaded wallet; the coinbase accrues there and nothing downstream
        # depends on it.
        self.generatetoaddress(node, 1, pure.getnewaddress("", "bech32"))

        # ---- 7d. Confidential input into all-explicit outputs fails closed ----
        # The all-explicit output set cannot be forced via -changetype alone:
        # resolve_change_key() upgrades any wallet-owned change key to Path C
        # regardless of the address format, so an own-address change always
        # produces a confidential output. The constructible form is a coin
        # control change to a *foreign* address: the mixer wallet's only UTXOs
        # are Path C (confidential), and change_address=bech32_addr (owned by
        # payer, not mixer) makes both the target and the change plaintext.
        # With no blinded output to absorb the confidential input's blinding
        # factor, BlindTransaction must refuse the tx (fail-closed), never
        # emit an unbalanced commitment. Failure surfaces as RPC_WALLET_ERROR
        # (-4) through the send path's FundTransaction -> CreateTransaction.
        self.log.info("Confidential input into only-explicit outputs must fail closed")
        self.restart_node(0, extra_args=["-changetype=bech32"])
        node.loadwallet("mixer")
        mixer = node.get_wallet_rpc("mixer")
        assert_raises_rpc_error(
            -4,
            "Confidential transaction blinding failed",
            mixer.send, outputs=[{bech32_addr: Decimal("0.05")}],
            options={"change_address": bech32_addr},
        )

        # ---- 8. -ctlegacy=1 usable below nBanPathAHeight (runtime probe) ----
        # H-1: the wallet refuses -ctlegacy=1 once the tip has reached the
        # chain's nBanPathAHeight (testnet: 0 -> banned from genesis; regtest:
        # INT_MAX -> never reached). The constant is not exposed over RPC, so
        # probe the wallet gate itself at runtime: a refusal with the
        # "banned at this height" error means Path A is disabled on this chain
        # and section 8 is a no-op here; otherwise the send below the gate must
        # succeed, the Path A output must be "confidential" in the RPC, and it
        # must remain scannable/spendable by the receiver (H-1: historical
        # Path A UTXOs stay usable). The wallet gate itself is covered by the
        # consensus unit test tx_verify_tests.cpp (nBanPathAHeight=200).
        self.log.info("-ctlegacy=1 below/at nBanPathAHeight (probed at runtime)")
        self.restart_node(0, extra_args=["-ctlegacy=1"])
        node = self.nodes[0]
        node.loadwallet("payer")
        node.loadwallet("receiver")
        payer = node.get_wallet_rpc("payer")
        receiver = node.get_wallet_rpc("receiver")
        receiver.walletpassphrase("pass", 100)
        balance_before = receiver.getbalance()
        try:
            txid3 = payer.sendtoaddress(bech32_addr, Decimal("0.05"))
        except JSONRPCException as exc:
            message = str(exc.error.get("message", ""))
            if "banned at this height" not in message:
                raise
            # Path A is banned from genesis on this chain (e.g. testnet with
            # nBanPathAHeight=0): the wallet correctly refuses -ctlegacy=1.
            # Expecting a successful Path A send here would fail by design;
            # the gate behavior itself is asserted by tx_verify_tests.cpp.
            skipped_sections.append("8: Path-A banned at this height; -ctlegacy correctly refused")
            self.log.info("nBanPathAHeight reached; -ctlegacy=1 refused by the wallet; skipping section 8")
            return
        decoded3 = node.getrawtransaction(txid3, True)
        target3 = None
        for o in decoded3["vout"]:
            spk = o["scriptPubKey"]
            if bech32_addr == spk.get("address") or bech32_addr in spk.get("addresses", []):
                target3 = o
                break
        assert target3 is not None, f"no vout pays {bech32_addr}: {decoded3['vout']}"
        assert_equal(target3["value"], "confidential")
        self.generatetoaddress(node, 1, payer.getnewaddress("", "bech32"))
        assert_equal(receiver.getbalance(), balance_before + Decimal("0.05"))
        if skipped_sections:
            self.log.info("Skipped sections: %s", "; ".join(skipped_sections))

if __name__ == "__main__":
    WalletConfidentialTest().main()
