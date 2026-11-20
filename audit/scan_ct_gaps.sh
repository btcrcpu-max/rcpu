#!/bin/bash
# Fail-closed entry point for the RCPU historical CT gap scan.
#
# The scan itself lives in scan_ct_gaps.py because the RPC JSON does not expose
# CT commitment bytes (see core_write.cpp); the previous jq-based version of
# this script could never observe a commitment and therefore always reported
# PASS. Parsing raw transactions lets the scan fail closed: any RPC error,
# malformed byte stream or unknown serialisation prefix aborts with exit 2.
#
# Run on S1 (mainnet node with txindex=1):
#   RCPU_CLI=/usr/local/bin/rcpu-cli RCPU_DATADIR=/root/.rcpu ./audit/scan_ct_gaps.sh
#
# Exit codes: 0 = fully scanned, no hits; 1 = hits found; 2 = scan aborted.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${PYTHON:-python3}"

exec "$PYTHON" "$HERE/scan_ct_gaps.py" "$@"
