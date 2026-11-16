#!/usr/bin/env python3
# Copyright (c) 2026 The RCPU developers
# Distributed under the MIT license, see the accompanying file COPYING.
"""Replay RCPU dual-algo difficulty against a running node.

Compares, for every block from the dual-algo activation height to the tip:

  * the pre-H-1 rule (absolute ASERT anchored at activation, no re-anchor);
  * the H-1 rule (re-anchor to the last same-algorithm block, capped drop);
  * the nBits actually stored in the chain.

This is an analysis tool, not consensus code. It needs a node with
`getblockhash` and `getblockheader` available (cookie or rpcuser/rpcpassword).
Use it before tagging an H-1 release: the H-1 column must match the chain only
from the configured activation height onward, and the pre-H-1 column must match
it before that height.

Example (cookie auth, default mainnet ports):

  python3 contrib/devtools/dual_algo_replay.py \
      --datadir ~/.rcpu/rcpu --start-height 20999

Example (explicit RPC auth):

  python3 contrib/devtools/dual_algo_replay.py \
      --rpcuser rcpu --rpcpassword secret --start-height 20999
"""

import argparse
import base64
import json
import os
import sys
import urllib.request

POW_LIMIT_MAINNET = 0x00003FFFFC000000000000000000000000000000000000000000000000000000


def set_compact(compact: int) -> int:
    size = compact >> 24
    word = compact & 0x007FFFFF
    if size <= 3:
        return word >> (8 * (3 - size))
    return word << (8 * (size - 3))


def get_compact(target: int) -> int:
    if target <= 0:
        return 0
    size = (target.bit_length() + 7) // 8
    if size <= 3:
        compact = target << (8 * (3 - size))
    else:
        compact = target >> (8 * (size - 3))
    if compact & 0x00800000:
        compact >>= 8
        size += 1
    return compact | (size << 24)


def trunc_div(a: int, b: int) -> int:
    """C++ integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def calculate_asert(ref_target: int, spacing: int, time_diff: int,
                    height_diff: int, pow_limit: int, half_life: int) -> int:
    """Port of CalculateASERT() in src/pow.cpp (Python ints are unbounded)."""
    assert ref_target > 0 and ref_target <= pow_limit
    assert height_diff >= 0
    max_mul = (2 ** 63 - 1) // 65536
    overflow_high = False
    overflow_low = False
    time_delta = 0
    h1 = height_diff + 1
    if spacing > 0 and h1 > 0 and spacing > (2 ** 63 - 1) // h1:
        overflow_low = True
    else:
        ideal = spacing * h1
        if time_diff >= 0 and time_diff > (2 ** 63 - 1):
            overflow_high = True
        elif time_diff < 0 and ideal > 0 and time_diff < -(2 ** 63) + ideal:
            overflow_low = True
        else:
            time_delta = time_diff - ideal
    if overflow_high or time_delta > max_mul:
        return pow_limit
    if overflow_low or time_delta < -max_mul:
        return 1
    exponent = trunc_div(time_delta * 65536, half_life)
    shifts = exponent >> 16
    frac = exponent & 0xFFFF
    factor = 65536 + ((195766423245049 * frac
                       + 971821376 * frac * frac
                       + 5127 * frac * frac * frac
                       + (1 << 47)) >> 48)
    target = ref_target * factor
    shifts -= 16
    if shifts > 512:
        target = pow_limit
    elif shifts < -512:
        target = 1
    elif shifts <= 0:
        target >>= -shifts
    else:
        target <<= shifts
    if target > pow_limit:
        target = pow_limit
    if target == 0:
        target = 1
    return target


class Rpc:
    def __init__(self, url: str, user: str, password: str):
        self.url = url
        token = base64.b64encode(f"{user}:{password}".encode()).decode()
        self.auth = "Basic " + token
        self.next_id = 0

    def batch(self, calls):
        payload = []
        for method, params in calls:
            payload.append({"jsonrpc": "1.0", "id": self.next_id,
                            "method": method, "params": params})
            self.next_id += 1
        req = urllib.request.Request(
            self.url,
            data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json",
                     "Authorization": self.auth},
        )
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = json.loads(resp.read().decode())
        by_id = {entry["id"]: entry for entry in body}
        out = []
        for entry in payload:
            reply = by_id.get(entry["id"], {})
            if "error" in reply and reply["error"]:
                raise RuntimeError(f"{entry['method']}: {reply['error']}")
            out.append(reply.get("result"))
        return out


def parse_header(hexstr: str):
    raw = bytes.fromhex(hexstr)
    if len(raw) != 112:
        raise ValueError(f"expected 112-byte RCPU header, got {len(raw)}")
    version = int.from_bytes(raw[0:4], "little", signed=True)
    time = int.from_bytes(raw[68:72], "little")
    bits = int.from_bytes(raw[72:76], "little")
    return version, time, bits


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="http://127.0.0.1:7337")
    ap.add_argument("--rpcuser")
    ap.add_argument("--rpcpassword")
    ap.add_argument("--datadir", help="read .cookie from this datadir")
    ap.add_argument("--start-height", type=int, default=20999,
                    help="first header height to fetch (default: activation - 1)")
    ap.add_argument("--activation-height", type=int, default=21000)
    ap.add_argument("--fix-height", type=int, default=30000,
                    help="H-1 activation height used for the new column")
    ap.add_argument("--spacing", type=int, default=600)
    ap.add_argument("--half-life", type=int, default=2 * 24 * 60 * 60)
    ap.add_argument("--reanchor-gap", type=int, default=12 * 60 * 60)
    ap.add_argument("--max-drop", type=int, default=8)
    ap.add_argument("--pow-limit", type=lambda s: int(s, 16),
                    default=POW_LIMIT_MAINNET)
    ap.add_argument("--chunk", type=int, default=500)
    args = ap.parse_args()

    user, password = args.rpcuser, args.rpcpassword
    if not user and args.datadir:
        cookie = os.path.join(args.datadir, ".cookie")
        with open(cookie, encoding="utf-8") as f:
            user, password = f.read().strip().split(":", 1)
    if not user:
        ap.error("provide --rpcuser/--rpcpassword or --datadir")
    if args.start_height > args.activation_height - 1:
        ap.error("--start-height must be <= --activation-height - 1 "
                 "(the activation anchor is needed)")

    rpc = Rpc(args.url, user, password)
    tip = rpc.batch([("getblockcount", [])])[0]
    if tip < args.activation_height:
        print(f"tip {tip} is below activation {args.activation_height}; nothing to replay")
        return 0
    heights = list(range(args.start_height, tip + 1))
    print(f"fetching {len(heights)} headers from {args.start_height} to {tip}...")

    hashes = []
    for i in range(0, len(heights), args.chunk):
        chunk = heights[i:i + args.chunk]
        hashes.extend(rpc.batch([("getblockhash", [h]) for h in chunk]))

    hexes = []
    for i in range(0, len(hashes), args.chunk):
        chunk = hashes[i:i + args.chunk]
        hexes.extend(rpc.batch([("getblockheader", [h, False]) for h in chunk]))

    # Index by height for direct access.
    version = {}
    time = {}
    bits = {}
    for h, hexstr in zip(heights, hexes):
        version[h], time[h], bits[h] = parse_header(hexstr)

    act = args.activation_height
    # prefix_q[h] = number of bit-30 blocks in [act, h]; 0 for h < act.
    prefix_q = {act - 1: 0}
    for h in range(act, tip + 1):
        prefix_q[h] = prefix_q[h - 1] + (1 if version[h] & 0x40000000 else 0)

    def cum_algo(h: int, qhash: bool) -> int:
        if h < act:
            return 0
        q = prefix_q[h]
        total = h - (act - 1)
        return q if qhash else total - q

    pow_limit = args.pow_limit
    old_mismatch = []
    new_mismatch = []
    first_new_diff = None
    reanchors = 0
    old_targets = {}
    new_targets = {}

    # New-rule per-algorithm state.
    last_h = {False: -1, True: -1}
    anchor_h = {False: act - 1, True: act - 1}
    for h in range(act, tip + 1):
        qhash = bool(version[h] & 0x40000000)
        # --- old rule: activation anchor, absolute schedule ---
        anchor = act - 1
        ref = set_compact(bits[anchor]) >> 1
        ref = 1 if ref == 0 else min(ref, pow_limit)
        q = prefix_q[h - 1]
        total = h - 1 - (act - 1)
        hdiff = q if qhash else total - q
        tdiff = time[h - 1] - time[anchor]
        old_t = calculate_asert(ref, args.spacing, tdiff, hdiff, pow_limit,
                                args.half_life)
        old_targets[h] = old_t
        if get_compact(old_t) != bits[h]:
            old_mismatch.append(h)

        # --- new rule: re-anchor after a long same-algorithm gap ---
        same = last_h[qhash]
        gap = time[h - 1] - time[same] if same >= 0 else 0
        reanchor = (h >= args.fix_height and same >= 0
                    and gap > args.reanchor_gap)
        if reanchor:
            anchor = same
            ref = min(set_compact(bits[same]), pow_limit)
            ref = 1 if ref == 0 else ref
            hdiff = 0
            tdiff = gap
            new_t = calculate_asert(ref, args.spacing, tdiff, hdiff,
                                    pow_limit, args.half_life)
            max_target = min(pow_limit, ref * args.max_drop)
            if new_t > max_target:
                new_t = max_target
            reanchors += 1
        else:
            hdiff = cum_algo(h - 1, qhash) - cum_algo(anchor_h[qhash], qhash)
            if hdiff < 0:
                hdiff = 0
            tdiff = time[h - 1] - time[anchor_h[qhash]]
            ref = set_compact(bits[anchor_h[qhash]])
            if anchor_h[qhash] == act - 1:
                ref >>= 1
            ref = 1 if ref == 0 else min(ref, pow_limit)
            new_t = calculate_asert(ref, args.spacing, tdiff, hdiff,
                                    pow_limit, args.half_life)
        new_targets[h] = new_t
        if get_compact(new_t) != bits[h]:
            new_mismatch.append(h)
        if h < args.fix_height and old_targets[h] != new_targets[h]:
            first_new_diff = first_new_diff or h

        # Update state for the next height.
        last_h[qhash] = h
        if reanchor:
            anchor_h[qhash] = h

    print(f"tip={tip} activation={act} fix={args.fix_height} "
          f"reanchor_gap={args.reanchor_gap}s max_drop={args.max_drop}x")
    print(f"old-rule nBits mismatches: {len(old_mismatch)}"
          + (f" (first {old_mismatch[:10]})" if old_mismatch else ""))
    print(f"new-rule nBits mismatches: {len(new_mismatch)}"
          + (f" (first {new_mismatch[:10]})" if new_mismatch else ""))
    print(f"new-rule re-anchors in range: {reanchors}")
    if first_new_diff is not None:
        print(f"WARNING: old/new targets differ before the fix height at {first_new_diff}"
              " -- replay code or parameters are inconsistent")
    if new_mismatch:
        print("The H-1 column does not reproduce the chain. This is expected if"
              " the node is still running the old rules before the fix height,"
              " or if the fix parameters differ from the release.")
    return 0 if not new_mismatch else 1


if __name__ == "__main__":
    sys.exit(main())
