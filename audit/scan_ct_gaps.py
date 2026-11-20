#!/usr/bin/env python3
"""Fail-closed historical CT gap scan for RCPU mainnet.

Why this exists
---------------
The previous version of scan_ct_gaps.sh used `getblock ... 2` JSON and looked
for `valuecommitment` / `noncecommitment` fields. Those fields do not exist:
core_write.cpp never serialises CTxIn::nValue, CTxOut::nNonce or
CTxOut::vchRangeproof, so every jq filter was vacuously false and the script
reported PASS without being able to see a single commitment. This scanner
parses raw transaction bytes instead.

Checks
------
F-1  v3 transaction with a non-null CTxIn::nValue.
     Unconditional rule (consensus/tx_check.cpp). Default range: 20365..tip.
F-2  v3 transaction signing SIGHASH_SINGLE / SINGLE|ANYONECANPAY.
     Default range: 0..9991.
F-3  v3 output with a confidential value commitment (prefix 8 or 9) but a null
     nonce commitment, below nCTStrictRulesHeight (27326 on mainnet).
     Bytes only: deciding whether an X coordinate is on the curve needs
     secp256k1, so off-curve nonces are out of scope here.

Fail-closed contract
--------------------
Any RPC failure, malformed hex, unrecognised serialisation prefix, oversized
range proof, or a transaction that does not consume its bytes exactly raises
ScanError, which aborts with exit code 2. A block only counts as checked after
every one of its v3 transactions was fully parsed. Exit code 1 means hits were
found; exit code 0 is reserved for "everything in range was actually parsed".

The node must serve getrawtransaction for confirmed blocks (txindex=1).

Usage
-----
    ./audit/scan_ct_gaps.sh                    # defaults below
    ./audit/scan_ct_gaps.sh --tip 30000        # explicit upper bound
    RCPU_CLI=/usr/local/bin/rcpu-cli RCPU_DATADIR=/root/.rcpu ./audit/scan_ct_gaps.sh
"""

import argparse
import json
import os
import subprocess
import sys
from typing import List, Tuple

CT_VERSION = 3

# CConfidentialValue: <ExplicitSize=9, PrefixA=8, PrefixB=9, PrefixC=0xFF>
VALUE_EXPLICIT = 1
VALUE_PREFIXES = (0, VALUE_EXPLICIT, 8, 9)
VALUE_COMMITMENT_PREFIXES = (8, 9)
# CConfidentialNonce: <ExplicitSize=33, PrefixA=2, PrefixB=3, PrefixC=4>
NONCE_PREFIXES = (0, 1, 2, 3, 4)

MAX_RANGEPROOF_SIZE = 5134


class ScanError(RuntimeError):
    pass


class Reader:
    def __init__(self, data: bytes, what: str):
        self.data = data
        self.pos = 0
        self.what = what

    def take(self, n: int) -> bytes:
        if n < 0 or self.pos + n > len(self.data):
            raise ScanError(f"{self.what}: truncated at offset {self.pos} "
                            f"(need {n}, have {len(self.data) - self.pos})")
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u8(self) -> int:
        return self.take(1)[0]

    def u32(self) -> int:
        return int.from_bytes(self.take(4), "little")

    def compact_size(self) -> int:
        first = self.u8()
        if first < 0xfd:
            return first
        if first == 0xfd:
            return int.from_bytes(self.take(2), "little")
        if first == 0xfe:
            return int.from_bytes(self.take(4), "little")
        return int.from_bytes(self.take(8), "little")

    def blob(self) -> bytes:
        return self.take(self.compact_size())

    def eof(self) -> bool:
        return self.pos == len(self.data)


class Confidential:
    """One CConfidentialCommitment: prefix byte plus its fixed-size payload."""

    __slots__ = ("prefix", "payload")

    def __init__(self, prefix: int, payload: bytes):
        self.prefix = prefix
        self.payload = payload

    @property
    def is_null(self) -> bool:
        return self.prefix == 0


def read_confidential(r: Reader, prefixes: Tuple[int, ...], what: str) -> Confidential:
    prefix = r.u8()
    if prefix not in prefixes:
        raise ScanError(f"{what}: unrecognised confidential prefix {prefix:#04x}")
    if prefix == 0:
        return Confidential(0, b"")
    size = 8 if prefix == VALUE_EXPLICIT else 32
    return Confidential(prefix, r.take(size))


class TxInfo:
    __slots__ = ("vin_values", "vout_values", "vout_nonces", "has_single")

    def __init__(self):
        self.vin_values: List[Confidential] = []
        self.vout_values: List[Confidential] = []
        self.vout_nonces: List[Confidential] = []
        self.has_single = False


def script_pushes(script: bytes) -> List[bytes]:
    pushes = []
    i = 0
    while i < len(script):
        op = script[i]
        if 1 <= op <= 75:
            pushes.append(script[i + 1:i + 1 + op])
            i += 1 + op
        elif op == 0x4c:  # OP_PUSHDATA1
            if i + 1 >= len(script):
                break
            ln = script[i + 1]
            pushes.append(script[i + 2:i + 2 + ln])
            i += 2 + ln
        elif op == 0x4d:  # OP_PUSHDATA2
            if i + 2 >= len(script):
                break
            ln = int.from_bytes(script[i + 1:i + 3], "little")
            pushes.append(script[i + 3:i + 3 + ln])
            i += 3 + ln
        elif op == 0x4e:  # OP_PUSHDATA4
            if i + 4 >= len(script):
                break
            ln = int.from_bytes(script[i + 1:i + 5], "little")
            pushes.append(script[i + 5:i + 5 + ln])
            i += 5 + ln
        else:
            i += 1
    return pushes


def sighash_class(data: bytes):
    if len(data) >= 9 and data[0] == 0x30:
        return ("ecdsa", data[-1])
    if len(data) == 64:
        return ("schnorr", 0)
    if len(data) == 65:
        return ("schnorr", data[-1])
    return None


def is_single(kind: str, hashtype: int) -> bool:
    if kind == "ecdsa":
        return (hashtype & 0x1f) == 3
    return (hashtype & 0x03) == 3


def parse_tx(raw: bytes, txid: str) -> TxInfo:
    """Parse a serialised transaction, mirroring UnserializeTransaction()."""
    r = Reader(raw, f"tx {txid}")
    info = TxInfo()

    version = int.from_bytes(r.take(4), "little", signed=True)
    f_ct = version == CT_VERSION

    def read_vin() -> List[Tuple[bytes, List[bytes]]]:
        count = r.compact_size()
        out = []
        for _ in range(count):
            r.take(32)                       # prevout.hash
            r.u32()                          # prevout.n
            script_sig = r.blob()
            r.u32()                          # nSequence
            if f_ct:
                info.vin_values.append(
                    read_confidential(r, VALUE_PREFIXES, f"tx {txid} vin nValue"))
            out.append((script_sig, []))
        return out

    def read_vout() -> None:
        count = r.compact_size()
        for _ in range(count):
            if f_ct:
                info.vout_values.append(
                    read_confidential(r, VALUE_PREFIXES, f"tx {txid} vout nValue"))
                info.vout_nonces.append(
                    read_confidential(r, NONCE_PREFIXES, f"tx {txid} vout nNonce"))
                proof_len = r.compact_size()
                if proof_len > MAX_RANGEPROOF_SIZE:
                    raise ScanError(f"tx {txid}: rangeproof {proof_len} > {MAX_RANGEPROOF_SIZE}")
                r.take(proof_len)
            r.blob()                         # scriptPubKey

    inputs = read_vin()
    flags = 0
    if not inputs:
        # Zero vin means the next byte is a flags marker (BIP 144 layout).
        flags = r.u8()
        if flags != 0:
            inputs = read_vin()
            read_vout()
    else:
        read_vout()

    if flags & 1:
        for i in range(len(inputs)):
            stack = r.compact_size()
            inputs[i] = (inputs[i][0], [r.blob() for _ in range(stack)])

    r.take(4)  # nLockTime
    if not r.eof():
        raise ScanError(f"tx {txid}: {len(raw) - r.pos} trailing byte(s)")

    for script_sig, witness_items in inputs:
        candidates = [x for x in script_pushes(script_sig)]
        candidates.extend(witness_items)
        for data in candidates:
            cls = sighash_class(data)
            if cls is not None and is_single(*cls):
                info.has_single = True
    return info


def cli(args: List[str], cli_path: str, datadir: str) -> str:
    cmd = [cli_path, f"-datadir={datadir}"] + args
    try:
        res = subprocess.run(cmd, check=False, capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.SubprocessError) as exc:
        raise ScanError(f"rpc {cmd[0]} could not run: {exc}")
    if res.returncode != 0:
        raise ScanError(f"rpc {' '.join(args)} failed ({res.returncode}): {res.stderr.strip()}")
    return res.stdout.strip()


def scan_height(opts, height: int) -> Tuple[List[str], int]:
    """Return (hits, v3 transaction count) for one height."""
    block_hash = cli(["getblockhash", str(height)], opts.cli, opts.datadir)
    block = json.loads(cli(["getblock", block_hash, "2"], opts.cli, opts.datadir))
    txs = block.get("tx")
    if not isinstance(txs, list):
        raise ScanError(f"height {height}: getblock returned no tx list")
    if not txs or txs[0].get("txid") is None:
        raise ScanError(f"height {height}: getblock returned malformed tx list")

    hits: List[str] = []
    v3_count = 0
    for tx in txs:
        if int(tx.get("version", -1)) != CT_VERSION:
            continue
        v3_count += 1
        txid = tx["txid"]
        raw_hex = cli(["getrawtransaction", txid, "0"], opts.cli, opts.datadir)
        try:
            raw = bytes.fromhex(raw_hex)
        except ValueError as exc:
            raise ScanError(f"height {height} tx {txid}: invalid hex ({exc})")
        info = parse_tx(raw, txid)

        if opts.f1_start <= height <= opts.f1_end:
            for idx, value in enumerate(info.vin_values):
                if not value.is_null:
                    hits.append(f"F-1 height={height} tx={txid} vin={idx} nValue={value.prefix:#04x}")
        if opts.f2_start <= height <= opts.f2_end and info.has_single:
            hits.append(f"F-2 height={height} tx={txid} sighash=SINGLE")
        if height < opts.strict_height:
            for idx, (value, nonce) in enumerate(zip(info.vout_values, info.vout_nonces)):
                if value.prefix in VALUE_COMMITMENT_PREFIXES and nonce.is_null:
                    hits.append(f"F-3 height={height} tx={txid} vout={idx} committed value, null nonce")
    return hits, v3_count


def main() -> int:
    ap = argparse.ArgumentParser(description="Fail-closed historical CT gap scan.")
    ap.add_argument("--cli", default=os.environ.get("RCPU_CLI", "/usr/local/bin/rcpu-cli"))
    ap.add_argument("--datadir", default=os.environ.get("RCPU_DATADIR", "/root/.rcpu"))
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--tip", type=int, default=None, help="last height (default: getblockcount)")
    ap.add_argument("--f1-start", type=int, default=20365)
    ap.add_argument("--f2-start", type=int, default=0)
    ap.add_argument("--f2-end", type=int, default=9991)
    ap.add_argument("--strict-height", type=int, default=27326,
                    help="mainnet nCTStrictRulesHeight; F-3 only scans below it")
    opts = ap.parse_args()

    if opts.tip is None:
        opts.tip = int(cli(["getblockcount"], opts.cli, opts.datadir))
    opts.f1_end = opts.tip

    print("=== RCPU historical CT gap scan (raw parsing, fail-closed) ===")
    print(f"range {opts.start}..{opts.tip} f1>={opts.f1_start} "
          f"f2={opts.f2_start}..{opts.f2_end} strict<{opts.strict_height}")

    all_hits: List[str] = []
    v3_total = 0
    for height in range(opts.start, opts.tip + 1):
        hits, v3_count = scan_height(opts, height)
        v3_total += v3_count
        all_hits.extend(hits)
        if height % 500 == 0 or hits:
            print(f"  height {height}/{opts.tip} v3_txs={v3_total} hits={len(all_hits)}", flush=True)

    print(f"v3 transactions parsed: {v3_total}")
    for hit in all_hits:
        print("HIT " + hit)
    if all_hits:
        print("RESULT: FAIL — hits found.")
        return 1
    print("RESULT: PASS — every block in range was fully parsed and no violation was found.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ScanError as exc:
        print(f"ABORT: {exc}", file=sys.stderr)
        print("RESULT: ERROR — scan incomplete, do NOT treat as PASS.", file=sys.stderr)
        sys.exit(2)
