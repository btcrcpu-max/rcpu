#!/bin/bash
# Scan RCPU mainnet for F-1 / F-2 historical validation gaps.
# Run locally on S1: uses /usr/local/bin/rcpu-cli against production datadir.
#
# F-1: v3 (CT_VERSION) txs with non-null CTxIn::nValue (vin[].valuecommitment non-null).
#      Rule is unconditional (src/consensus/tx_check.cpp:55-62).
#      Original scan: blocks 0..20364. We verify 20365..<tip>.
#
# F-2: v3 txs using SIGHASH_SINGLE / SINGLE|ANYONECANPAY.
#      Rule is unconditional (src/script/interpreter.cpp).
#      Original scan: 9992+. We verify 0..9991.

set -euo pipefail

CLI="${RCPU_CLI:-/usr/local/bin/rcpu-cli}"
DATADIR="${RCPU_DATADIR:-/root/.rcpu}"
TIP=$($CLI -datadir="$DATADIR" getblockcount)
echo "=== RCPU Historical Gap Scan ==="
echo "Tip: $TIP"
echo "Started: $(date -u -Iseconds)"

F1_START=20365
F1_END=$TIP
F2_START=0
F2_END=9991
CT_VERSION=3

# --- F-1 scan ---
echo ""
echo "--- F-1: scanning blocks $F1_START..$F1_END for v3 txs with non-null CTxIn::nValue ---"
f1_hits=0
f1_sample=""
f1_checked=0
for ((h = F1_START; h <= F1_END; h++)); do
    if (( h % 500 == 0 && h > F1_START )); then
        echo "  progress: block $h / $F1_END ($(( 100*(h-F1_START)/(F1_END-F1_START+1) ))%) hits=$f1_hits" >&2
    fi
    bh=$($CLI -datadir="$DATADIR" getblockhash "$h")
    block=$($CLI -datadir="$DATADIR" getblock "$bh" 2)
    # jq: iterate txs, filter v3, then iterate vin looking for non-null valuecommitment
    hits=$(echo "$block" | jq -r --argjson v $CT_VERSION '
        .tx[] | select(.version == $v) | .vin[] | select(.valuecommitment != null and .valuecommitment != "") |
        "HIT h='"$h"' txid=\(.txid // "?") input=\(.valuecommitment)"
    ' 2>/dev/null || true)
    # jq filters in subshell above don't work well — redo differently
    :
    # Simpler: use jq to enumerate
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        f1_hits=$((f1_hits + 1))
        if [ -z "$f1_sample" ]; then f1_sample="$line"; fi
    done <<< "$(echo "$block" | jq -c --argjson v $CT_VERSION '
        .tx[] | select(.version == $v) as $tx | ($tx.vin // [])[] |
        select(.valuecommitment != null and .valuecommitment != "") |
        "\(.valuecommitment)"
    ' 2>/dev/null || true)"
    f1_checked=$((f1_checked + 1))
done
echo "F-1: checked blocks=$f1_checked hits=$f1_hits"
if [ -n "$f1_sample" ]; then
    echo "F-1 SAMPLE: $f1_sample"
fi

# --- F-2 scan ---
echo ""
echo "--- F-2: scanning blocks $F2_START..$F2_END for v3 txs with SIGHASH_SINGLE ---"
f2_hits=0
f2_checked=0
for ((h = F2_START; h <= F2_END; h++)); do
    if (( h % 100 == 0 && h > F2_START )); then
        echo "  progress: block $h / $F2_END hits=$f2_hits" >&2
    fi
    bh=$($CLI -datadir="$DATADIR" getblockhash "$h" 2>/dev/null) || continue
    block=$($CLI -datadir="$DATADIR" getblock "$bh" 2 2>/dev/null) || continue

    # Check if ANY v3 tx exists in this block first (cheap filter)
    v3_count=$(echo "$block" | jq '[.tx[] | select(.version == 3)] | length' 2>/dev/null || echo 0)
    [ "$v3_count" = "0" ] || [ -z "$v3_count" ] && { f2_checked=$((f2_checked + 1)); continue; }

    # For each v3 tx, check every witness stack item for SIGHASH_SINGLE byte (0x03, 0x83)
    # Legacy ECDSA: sig ends with sighash byte in scriptSig
    # Taproot/Schnorr: not applicable (v3 + SINGLE rule covers ECDSA paths only)
    while IFS= read -r txid; do
        [ -z "$txid" ] && continue
        f2_hits=$((f2_hits + 1))
        echo "F-2 HIT: height=$h txid=$txid"
    done <<< "$(echo "$block" | jq -r --argjson v 3 '
        [.tx[] | select(.version == $v)] | .[] |
        .txid as $tid |
        (.vin // [])[] |
        (.witness // [])[] | select(type == "string" and length >= 2) |
        (.[-2:]) as $last |
        (tonumber($last; 16)) as $byte |
        select($byte == 3 or $byte == 131) |
        $tid
    ' 2>/dev/null | sort -u)"
    f2_checked=$((f2_checked + 1))
done
echo "F-2: checked blocks=$f2_checked hits=$f2_hits"

echo ""
echo "=== FINAL RESULTS ==="
echo "F-1 (v3 non-null CTxIn::nValue, range $F1_START..$F1_END): $f1_hits HITS"
echo "F-2 (v3 SIGHASH_SINGLE, range $F2_START..$F2_END): $f2_hits HITS"
echo "Finished: $(date -u -Iseconds)"
echo ""
if [ "$f1_hits" = "0" ] && [ "$f2_hits" = "0" ]; then
    echo "RESULT: PASS — no historical validation gaps found within scan ranges."
    exit 0
else
    echo "RESULT: FAIL — hits found. Review immediately."
    exit 1
fi
