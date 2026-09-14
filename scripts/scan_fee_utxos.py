#!/usr/bin/env python3
"""
scan_fee_utxos.py - find fee-shaped outputs on the RCPU chain.

A fee-shaped output matches CTxOut::IsFee() (see src/primitives/transaction.h):

    nValue.IsExplicit() && scriptPubKey.empty()

i.e. an explicit (non-confidential) amount with an empty, unspendable script.

For every fee-shaped output ever seen in a block we ask gettxout() whether it
is still in the UTXO set:

    - still present -> "still unspent" (security alert, exit code 3)
    - absent        -> "spent/absent"  (already spent, or never entered the
                       UTXO set; fee outputs produced after the fix never
                       enter the UTXO set at all)

Requires a synced rcpud with a reachable RPC endpoint. Does NOT require
-txindex (fee output liveness is checked with gettxout only).

Usage:
    python3 scan_fee_utxos.py
    python3 scan_fee_utxos.py --rpc-url http://127.0.0.1:7337 --rpc-user U --rpc-password P
    python3 scan_fee_utxos.py --from-height 0 --to-height 10000 --json-out fee_utxo_report.json
    python3 scan_fee_utxos.py --unspent-only

Environment variables (overridden by command line arguments):
    RCPU_RPC_URL       RPC endpoint            (default: http://127.0.0.1:7337)
    RCPU_RPC_USER      RPC username
    RCPU_RPC_PASSWORD  RPC password

Exit codes:
    0  scan completed, no still-unspent fee-shaped UTXO found
    1  RPC error
    2  authentication failed / cookie file not found
    3  scan completed, found still-unspent fee-shaped UTXO(s)
"""

import argparse
import base64
import json
import os
import sys
import urllib.error
import urllib.request

DEFAULT_RPC_URL = "http://127.0.0.1:7337"
DEFAULT_COOKIE_PATH = os.path.join(os.path.expanduser("~"), ".rcpu", ".cookie")

ENV_URL = "RCPU_RPC_URL"
ENV_USER = "RCPU_RPC_USER"
ENV_PASSWORD = "RCPU_RPC_PASSWORD"

EXIT_OK = 0
EXIT_RPC_ERROR = 1
EXIT_AUTH_ERROR = 2
EXIT_UNSPENT_FOUND = 3

PROGRESS_EVERY = 250


class RpcError(Exception):
    """Raised on any JSON-RPC level failure."""


class AuthError(RpcError):
    """Raised when authentication fails or the cookie file is missing."""


class RpcClient:
    """Minimal JSON-RPC 1.0 client with HTTP Basic auth."""

    def __init__(self, url, username=None, password=None):
        self.url = url.rstrip("/")
        self.auth = None
        if username is not None:
            token = base64.b64encode(
                ("{}:{}".format(username, password or "")).encode("utf-8")
            ).decode("ascii")
            self.auth = "Basic " + token

    def call(self, method, params):
        payload = json.dumps({
            "jsonrpc": "1.0",
            "id": "scan_fee_utxos",
            "method": method,
            "params": params,
        }).encode("utf-8")
        req = urllib.request.Request(self.url, data=payload, method="POST")
        req.add_header("Content-Type", "application/json")
        if self.auth:
            req.add_header("Authorization", self.auth)
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                raw = resp.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            if exc.code == 401:
                raise AuthError("authentication failed (HTTP 401)")
            body = exc.read().decode("utf-8", "replace")
            raise RpcError("HTTP {}: {}".format(exc.code, body[:300]))
        except urllib.error.URLError as exc:
            raise RpcError("cannot reach RPC at {}: {}".format(self.url, exc.reason))
        try:
            result = json.loads(raw)
        except ValueError as exc:
            raise RpcError("invalid JSON-RPC response: {}".format(exc))
        if result.get("error") is not None:
            raise RpcError("RPC {} failed: {}".format(method, json.dumps(result["error"])))
        return result.get("result")


def load_cookie(path):
    """Read a Bitcoin-style .cookie file and return (username, password)."""
    if not os.path.isfile(path):
        raise AuthError("cookie file not found: {}".format(path))
    try:
        with open(path, "r", encoding="utf-8") as fh:
            line = fh.readline().strip()
    except OSError as exc:
        raise AuthError("cannot read cookie file {}: {}".format(path, exc))
    if ":" not in line:
        raise AuthError("cookie file {} has no 'user:pass' content".format(path))
    user, password = line.split(":", 1)
    return user, password


def build_client(args):
    """Resolve RPC endpoint/credentials from CLI args, env vars or cookie."""
    url = args.rpc_url or os.environ.get(ENV_URL) or DEFAULT_RPC_URL
    user = args.rpc_user or os.environ.get(ENV_USER)
    password = args.rpc_password or os.environ.get(ENV_PASSWORD)
    if user is None or password is None:
        user, password = load_cookie(DEFAULT_COOKIE_PATH)
    return RpcClient(url, username=user, password=password)


def is_fee_shaped(vout):
    """
    Match CTxOut::IsFee(): explicit (non-confidential) value + empty script.

    The RPC layer serializes an explicit amount as a JSON number while a
    confidential amount is the string "confidential" (see TxToUniv in
    src/core_write.cpp). An empty scriptPubKey serializes to an empty hex.
    """
    value = vout.get("value")
    if isinstance(value, str):
        return False  # "confidential" (or any non-numeric marker)
    if not isinstance(value, (int, float)):
        return False
    script_hex = (vout.get("scriptPubKey") or {}).get("hex", "")
    return script_hex == ""


def scan(rpc, from_height, to_height, unspent_only, json_out):
    """Scan the chain and report fee-shaped outputs. Returns exit code."""
    try:
        tip = rpc.call("getblockcount", [])
    except RpcError as exc:
        raise exc

    if to_height is None or to_height > tip:
        to_height = tip
    if from_height < 0:
        from_height = 0

    found_total = 0
    unspent = []
    spent_absent = 0

    if from_height <= to_height:
        for height in range(from_height, to_height + 1):
            block_hash = rpc.call("getblockhash", [height])
            # verbosity=2: full transaction details (value + scriptPubKey),
            # no -txindex required because the data comes from the block.
            block = rpc.call("getblock", [block_hash, 2])
            for tx in block.get("tx", []):
                txid = tx.get("txid", "")
                for vout in tx.get("vout", []):
                    if not is_fee_shaped(vout):
                        continue
                    found_total += 1
                    n = vout["n"]
                    # include_mempool=false: pure chain-state UTXO answer.
                    coin = rpc.call("gettxout", [txid, n, False])
                    if coin is None:
                        spent_absent += 1
                        continue
                    unspent.append({
                        "height": height,
                        "txid": txid,
                        "vout": n,
                        "value_btc": coin.get("value"),
                        "confirmations": coin.get("confirmations"),
                        "coinbase": bool(coin.get("coinbase", False)),
                    })
            if height % PROGRESS_EVERY == 0 or height == to_height:
                print("scanning height {}/{} ... fee-shaped found: {}, "
                      "still unspent: {}".format(height, to_height,
                                                found_total, len(unspent)),
                      file=sys.stderr)

    n_unspent = len(unspent)
    print("fee-shaped found: {}".format(found_total))
    print("still unspent: {}".format(n_unspent))
    if not unspent_only:
        print("spent/absent: {}".format(spent_absent))
    for u in unspent:
        print("UNSPENT height={} {}:{} value={} RCPU".format(
            u["height"], u["txid"], u["vout"], u["value_btc"]))

    if json_out:
        report = {
            "scan": {
                "from_height": from_height,
                "to_height": to_height,
                "chain_tip": tip,
            },
            "summary": {
                "fee_shaped_found": found_total,
                "still_unspent": n_unspent,
            },
            "unspent": unspent,
        }
        if not unspent_only:
            report["summary"]["spent_or_absent"] = spent_absent
        with open(json_out, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2)
            fh.write("\n")
        print("report written to {}".format(json_out))

    return EXIT_UNSPENT_FOUND if n_unspent else EXIT_OK


def main():
    parser = argparse.ArgumentParser(
        description="Scan the RCPU chain for fee-shaped outputs "
                    "(CTxOut::IsFee: explicit value + empty scriptPubKey).")
    parser.add_argument("--rpc-url", help="RPC endpoint "
                        "(default: {} or ${})".format(DEFAULT_RPC_URL, ENV_URL))
    parser.add_argument("--rpc-user", help="RPC username "
                        "(default: {} or cookie)".format(ENV_USER))
    parser.add_argument("--rpc-password", help="RPC password "
                        "(default: {} or cookie)".format(ENV_PASSWORD))
    parser.add_argument("--from-height", type=int, default=0,
                        help="first block height to scan (default: 0)")
    parser.add_argument("--to-height", type=int, default=None,
                        help="last block height to scan (default: chain tip)")
    parser.add_argument("--json-out",
                        help="write a JSON report to this file")
    parser.add_argument("--unspent-only", action="store_true",
                        help="only report outputs that are still unspent")
    args = parser.parse_args()

    try:
        rpc = build_client(args)
    except AuthError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return EXIT_AUTH_ERROR

    try:
        return scan(rpc, args.from_height, args.to_height,
                    args.unspent_only, args.json_out)
    except AuthError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return EXIT_AUTH_ERROR
    except RpcError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return EXIT_RPC_ERROR


if __name__ == "__main__":
    sys.exit(main())