#!/usr/bin/env bash
# Merge-day / tag-day guard for mainnet Path-A ban height.
# Usage: bash check-patha-buffer.sh
# Requires rcpu-cli against a synced mainnet node.
#
# v1.1.1 NOTE: Path-A ban is currently DEFERRED (INT_MAX).
# When re-scheduled, set H to the new target height and update MIN_BUF if needed.
set -euo pipefail

H=INT_MAX
MIN_BUF=2016

TIP=$(rcpu-cli getblockcount)

if [ "$H" = "INT_MAX" ]; then
  echo "tip=${TIP}"
  echo "H=${H} (DEFERRED — no activation scheduled)"
  echo "OK: ban is deferred, no buffer check needed."
  exit 0
fi

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
