#!/usr/bin/env python3
"""Scan RCPU mainnet blocks for v3 SIGHASH_SINGLE signatures."""
import json
import sys
import requests

CONF = "/root/.rcpu/rcpu/rcpu.conf"
URL = "http://127.0.0.1:7337/"
START = 9992
END = 27326
BATCH = 40


def load_auth():
    user = pw = None
    with open(CONF, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("rpcuser="):
                user = line.split("=", 1)[1]
            elif line.startswith("rpcpassword="):
                pw = line.split("=", 1)[1]
    if not user or not pw:
        raise SystemExit("rpc user/password not found")
    return (user, pw)


def iter_pushes(script):
    i = 0
    n = len(script)
    while i < n:
        op = script[i]
        if 1 <= op <= 75:
            data = script[i + 1:i + 1 + op]
            if len(data) == op:
                yield data
            i += 1 + op
        elif op == 0x4c:  # PUSHDATA1
            if i + 1 >= n:
                return
            ln = script[i + 1]
            data = script[i + 2:i + 2 + ln]
            if len(data) == ln:
                yield data
            i += 2 + ln
        elif op == 0x4d:  # PUSHDATA2
            if i + 2 >= n:
                return
            ln = int.from_bytes(script[i + 1:i + 3], "little")
            data = script[i + 3:i + 3 + ln]
            if len(data) == ln:
                yield data
            i += 3 + ln
        elif op == 0x4e:  # PUSHDATA4
            if i + 4 >= n:
                return
            ln = int.from_bytes(script[i + 1:i + 5], "little")
            data = script[i + 5:i + 5 + ln]
            if len(data) == ln:
                yield data
            i += 5 + ln
        else:
            i += 1


def sig_sighash(data):
    if not data:
        return None
    # ECDSA DER: 0x30 len ... sighash byte
    if len(data) >= 9 and data[0] == 0x30:
        return ("ecdsa", data[-1])
    # Schnorr: 64-byte default ALL, 65-byte has explicit sighash
    if len(data) == 64:
        return ("schnorr", 0)
    if len(data) == 65:
        return ("schnorr", data[-1])
    return None


def is_single(kind, ht):
    if kind == "ecdsa":
        return (ht & 0x1f) == 3
    return (ht & 0x03) == 3


def main():
    auth = load_auth()
    s = requests.Session()
    s.auth = auth
    hits = []
    v3_txs = 0
    v3_inputs = 0
    sigs = 0
    heights = list(range(START, END + 1))
    for base in range(0, len(heights), BATCH):
        chunk = heights[base:base + BATCH]
        b1 = [{"jsonrpc": "1.0", "id": h, "method": "getblockhash", "params": [h]} for h in chunk]
        r = s.post(URL, json=b1, timeout=60)
        r.raise_for_status()
        hm = {x["id"]: x.get("result") for x in r.json()}
        b2 = [{"jsonrpc": "1.0", "id": h, "method": "getblock", "params": [hm[h], 2]} for h in chunk]
        r = s.post(URL, json=b2, timeout=120)
        r.raise_for_status()
        bm = {x["id"]: x.get("result") for x in r.json()}
        for h in chunk:
            block = bm.get(h)
            if not block:
                continue
            for tx in block.get("tx", []):
                if int(tx.get("version", 0)) != 3:
                    continue
                v3_txs += 1
                for vi, vin in enumerate(tx.get("vin", [])):
                    v3_inputs += 1
                    candidates = []
                    ss = (vin.get("scriptSig") or {}).get("hex", "")
                    if ss:
                        try:
                            candidates.extend(("scriptSig", x) for x in iter_pushes(bytes.fromhex(ss)))
                        except ValueError:
                            pass
                    for w in vin.get("txinwitness") or []:
                        try:
                            candidates.append(("witness", bytes.fromhex(w)))
                        except ValueError:
                            pass
                    for where, data in candidates:
                        info = sig_sighash(data)
                        if not info:
                            continue
                        sigs += 1
                        kind, ht = info
                        if is_single(kind, ht):
                            hits.append({
                                "height": h,
                                "txid": tx.get("txid"),
                                "vin": vi,
                                "where": where,
                                "kind": kind,
                                "sighash": ht,
                                "prevout": vin.get("txid", "") + ":" + str(vin.get("vout", "")),
                            })
        print("progress %d/%d v3_txs=%d sigs=%d hits=%d" % (
            min(base + BATCH, len(heights)) + START - 1, END, v3_txs, sigs, len(hits)), flush=True)

    out = {
        "start": START, "end": END,
        "blocks": len(heights), "v3_txs": v3_txs, "v3_inputs": v3_inputs,
        "signatures_examined": sigs, "v3_single_hits": len(hits), "hits": hits,
    }
    with open("/root/v3_single_scan_9992_27326.json", "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    print(json.dumps({k: out[k] for k in ("start", "end", "blocks", "v3_txs", "v3_inputs", "signatures_examined", "v3_single_hits")}, indent=2))
    if hits:
        for x in hits[:20]:
            print("HIT", json.dumps(x, ensure_ascii=False))


if __name__ == "__main__":
    main()
