#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test for computerandomxhash dual-algo dispatch.

Verifies that the RPC correctly routes headers by algorithm:

 1. RandomX header (bit-30 clear): commitment != hash
    (commitment is the RandomX commitment, not the raw hash).
 2. Pre-activation bit-30 header: commitment != hash
    (bit-30 is ignored below the activation height; the header is
    treated as RandomX, so the commitment is the RandomX commitment).
 3. Post-activation Etchash header (bit-30 set): commitment == hash
    (Etchash has no separate RandomX commitment; the algo output IS
    the commitment).
 4. Post-activation RandomX header (bit-30 clear): commitment != hash.

The pre-activation bit-30 case is the regression guard: before the fix,
computerandomxhash routed ALL bit-30 headers to the Etchash verifier,
which would produce garbage for pre-activation headers (or headers with
unknown height) because the Etchash epoch could not be derived.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


def flip_bit30(header_hex):
    """Set bit-30 (0x40000000) in the nVersion field of a header hex string.

    RCPU header layout: 4-byte nVersion (LE) at offset 0, then the rest.
    """
    version_bytes = bytes.fromhex(header_hex[:8])
    version_int = int.from_bytes(version_bytes, 'little')
    version_int |= 0x40000000
    new_version_hex = version_int.to_bytes(4, 'little').hex()
    return new_version_hex + header_hex[8:]


class ComputeRandomXHashDualAlgoTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.chain = 'rcpuregtest'
        self.setup_clean_chain = True
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    @staticmethod
    def get_header_hex(node, block_hash):
        """Extract the 112-byte RCPU header from a raw block hex.

        RCPU header = 80 bytes legacy + 32 bytes hashRandomX = 112 bytes.
        """
        raw = node.getblock(block_hash, 0)
        return raw[:224]  # 112 bytes = 224 hex chars

    def run_test(self):
        n0 = self.nodes[0]
        n0.createwallet(wallet_name="miner", descriptors=True)
        addr = n0.get_wallet_rpc("miner").getnewaddress()

        # ---- Test 1: RandomX header (bit-30 clear), pre-activation ----
        self.log.info("Test 1: RandomX header -> commitment != hash")
        self.generatetoaddress(n0, 1, addr)
        tip = n0.getbestblockhash()
        header_hex = self.get_header_hex(n0, tip)
        result = n0.computerandomxhash(header_hex)
        assert result['hash'] != result['commitment'], (
            f"RandomX: commitment should differ from hash, "
            f"got hash={result['hash']} commitment={result['commitment']}"
        )

        # ---- Test 2: pre-activation bit-30 header -> RandomX path ----
        self.log.info("Test 2: pre-activation bit-30 header -> commitment != hash")
        height = n0.getblockcount()
        assert height < 150, f"Expected pre-activation, got height {height}"
        # Flip bit-30 on the current tip's header. The hashPrevBlock is still
        # on-chain so the RPC can determine the height, and since the height
        # is below activation it must use the RandomX path (bit ignored).
        bit30_header = flip_bit30(header_hex)
        result = n0.computerandomxhash(bit30_header)
        assert result['hash'] != result['commitment'], (
            f"Pre-activation bit-30: commitment should differ from hash "
            f"(RandomX path), got hash={result['hash']} commitment={result['commitment']}"
        )

        # ---- Mine past activation height (150) ----
        self.log.info("Mining past activation height (150)...")
        while n0.getblockcount() < 155:
            remaining = 155 - n0.getblockcount()
            chunk = min(remaining, 40)
            self.generatetoaddress(n0, chunk, addr)

        # ---- Test 3: post-activation Etchash header -> commitment == hash ----
        self.log.info("Test 3: post-activation Etchash header -> commitment == hash")
        n0.generatetoaddress(1, addr, 1000000, "etchash", invalid_call=False)
        etchash_tip = n0.getbestblockhash()
        etchash_header_hex = self.get_header_hex(n0, etchash_tip)
        result = n0.computerandomxhash(etchash_header_hex)
        assert_equal(result['hash'], result['commitment'])

        # ---- Test 4: post-activation RandomX header -> commitment != hash ----
        self.log.info("Test 4: post-activation RandomX header -> commitment != hash")
        n0.generatetoaddress(1, addr, 1000000, "randomx", invalid_call=False)
        rx_tip = n0.getbestblockhash()
        rx_header_hex = self.get_header_hex(n0, rx_tip)
        result = n0.computerandomxhash(rx_header_hex)
        assert result['hash'] != result['commitment'], (
            f"Post-activation RandomX: commitment should differ from hash, "
            f"got hash={result['hash']} commitment={result['commitment']}"
        )

        self.log.info("All computerandomxhash dual-algo dispatch tests passed")


if __name__ == '__main__':
    ComputeRandomXHashDualAlgoTest().main()
