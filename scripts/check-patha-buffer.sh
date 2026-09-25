#!/usr/bin/env bash
# Merge-day / tag-day guard for mainnet Path-A ban height.
# Usage: bash check-patha-buffer.sh
# Requires rcpu-cli against a synced mainnet node.
set -euo pipefail

H=9193
MIN_BUF=2016

TIP=$(rcpu-cli getblockcount)
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
