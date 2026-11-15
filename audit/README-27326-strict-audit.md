# RCPU 27,326 strict-nonce audit follow-up

Baseline: GitHub `main` commit `0d690b3` (v1.4.9).

## 1. Full-chain v3 SIGHASH_SINGLE scan (9992-27326)

Script: `audit/scan_v3_single.py`

RPC source: S3 node, height checked during scan.

Result:

```json
{
  "start": 9992,
  "end": 27326,
  "blocks": 17335,
  "v3_txs": 2587,
  "v3_inputs": 38438,
  "signatures_examined": 38438,
  "v3_single_hits": 0
}
```

No historical v3 `SIGHASH_SINGLE` / `SIGHASH_SINGLE|ANYONECANPAY`
signature was found in this range. `signatures_examined == v3_inputs`
means every v3 input yielded a parsed signature.

Note: blocks 0-9991 are below the shipped `defaultAssumeValid` (9992) and
were not part of this scan.

## 2. BlindTransaction Path-A gate

`BlindTransaction` now takes:

```cpp
int nSpendHeight = 0,
int nCTStrictRulesHeight = std::numeric_limits<int>::max()
```

and rejects the Path-A (`0x02 || nonce`) fallback when
`nSpendHeight >= nCTStrictRulesHeight`.

Wallet call passes the next block height and `consensus.nCTStrictRulesHeight`.
Legacy callers/tests keep the defaults and remain compatible.

## 3. -ctlegacy height

`wallet/spend.cpp` now computes:

```cpp
const int spend_height = wallet.GetLastBlockHeight() + 1;
```

and uses that for both `nBanPathAHeight` and `nCTStrictRulesHeight`, closing the
one-block window at the activation boundary.

## 4. Tests

- `tx_verify_tests.cpp`: new positive test
  `strict_nonce_rules_accept_valid_points` covers valid `0x03` compressed
  nonces and `0x04` x-only nonces at/after the strict height.
- `blind_tests.cpp`: new test `path_a_rejected_at_strict_height` verifies
  Path A is accepted pre-activation and rejected at activation.
- Existing negative tests for null, `0x02`, and off-curve nonces remain.

## Verification

A full `test_bitcoin` binary was built from this worktree with MSYS2 MinGW
16.2.0 (configure flags: `--without-gui --disable-bench --disable-zmq
--with-incompatible-bdb`, Boost include passed as `-idirafter` to avoid the
MinGW `#include_next` issue).

Results:

```text
tx_verify_tests: 15/15 test cases passed, 82/82 assertions passed
blind_tests:     30/30 test cases passed, 524/524 assertions passed
```

New tests:

```text
tx_verify_tests/strict_nonce_rules_accept_valid_points  PASS (5 assertions)
blind_tests/path_a_rejected_at_strict_height            PASS (2 assertions)
```

The four older blind_tests whose expectations predated the M-2 guard were
updated to the current fail-closed semantics (non-zero input blind for the
keyed Path B cases, explicit-input blind for the all-explicit subcase, and
big-endian scalar bytes for the paired-blind cancellation case).

## Chain observation

Public explorer block hashes are contiguous from 27324 through 27329
(tip observed 27329 during the audit; node `getblockcount` returned 27330
later). No split was visible at the time of the audit.
