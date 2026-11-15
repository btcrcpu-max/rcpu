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

`g++ -std=c++20 -fsyntax-only` (MSYS2 MinGW 16.2.0) passed for:

```text
src/blind.cpp
src/wallet/spend.cpp
src/test/tx_verify_tests.cpp
src/test/blind_tests.cpp
```

A full `test_bitcoin` binary was not available in this checkout, so the new
unit tests have not been executed here. Run `make check` in a configured
build before release.

## Chain observation

Public explorer block hashes are contiguous from 27324 through 27329
(tip observed 27329 during the audit; node `getblockcount` returned 27330
later). No split was visible at the time of the audit.
