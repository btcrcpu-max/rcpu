#!/usr/bin/env bash
# Merge-day / tag-day guard for mainnet Path-A ban height.
# Usage: bash check-patha-buffer.sh
# Requires rcpu-cli against a synced mainnet node.
#
# NOTE: Path-A ban activated at H=12748 (based on tip + 2016 buffer, v1.1.10).
set -euo pipefail

H=12748
MIN_BUF=2016

TIP=$(rcpu-cli getblockcount)

if [ "$H" = "INT_MAX" ]; then
  echo "tip=${TIP}"
  echo "H=${H} (DEFERRED — no activation scheduled)"
  echo "OK: ban is deferred, no buffer check needed."
  exit 0
fi

# NOTE: As of v1.1.10, H=12748 is fixed. The script continues to run the
# buffer check so it can be re-used for future consensus changes.

BUF=$((H - TIP))

echo "tip=${TIP}"
echo "H=${H}"
echo "buffer=${BUF} (need >= ${MIN_BUF})"

if [ "${BUF}" -lt "${MIN_BUF}" ]; then
  NEW=$((TIP + MIN_BUF))
  echo "FAIL: buffer too small. Set consensus.nBanPathAHeight = ${NEW} and amend before merge/tag."
  exit 1
fi

echo "OK"
