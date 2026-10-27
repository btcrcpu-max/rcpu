---
name: bitcoin-upstream-backport
description: Backport Bitcoin Core upstream PRs to the RCPU chain (27.0 baseline + CT/RandomX extensions). Use when the user asks to port an upstream Bitcoin PR, fix a CVE, or cherry-pick a bitcoin/bitcoin commit into this RCPU repo. Do not use for RCPU-only feature work or for audits that produce no code.
---

# Bitcoin Core Upstream Backport

RCPU is a fork of Bitcoin Core 27.0 with confidential transactions, RandomX/Etchash dual-algo mining, and other RCPU-specific changes. Upstream PRs must be adapted, not applied verbatim.

## Workflow

1. **Locate the upstream diff.** The user will give a PR number or CVE id. Fetch the patch via WebSearch/WebFetch (bitcoin/bitcoin PR page) and save the raw diff. Diffs from prior tool calls are often already on disk at `.trae-cn/work/.trae/toolcall-output/call_*.txt`.

2. **Identify the RCPU baseline differences** that affect the PR:
   - No PayToAnchor (`TxoutType::ANCHOR`), no `WitnessV16` (28.0+ features).
   - `CTransactionSignatureSerializer` and `SignatureHash` carry an `fCT` branch (`txTo.nVersion == CT_VERSION`); preserve it.
   - `CTxOut::Serialize` / `CTxIn::Serialize` take an explicit `bool fCT` parameter — never drop it.
   - `CTxDestination` variant has 9 alternatives (8 standard + RCPU `ConfidentialKeyHash`), not 10 (no `PayToAnchor`).
   - `CheckInputScripts` may have RCPU flags such as `SCRIPT_VERIFY_DISCOURAGE_ORDINALS` (`g_isRandomX ? ... : 0`).
   - `MaybePunishNodeForTx` returns `bool` and has two call sites (orphan + normal tx) on this base.
   - Test RNG: `FastRandomContext` in this tree exposes `randrange`/`rand32`/`rand64`/`randbool` but **not** `rand<T>()`.
   - `uint256` has **no** `operator<<` — `BOOST_CHECK_EQUAL(a, b)` with uint256 won't compile; use `BOOST_CHECK(a == b)`.
   - `Txid`/`Wtxid` types exist (backported), `CTransaction::GetHash()` returns `const Txid&`.
   - PowerShell 5 here has no `&&`; chain with `;`. No `grep`/`sed`/`python`/`python3` in PATH — use the Grep/Read tools and rely on CI for verification.

3. **Read the exact local context** before editing: use Read/Grep to confirm current line numbers, function signatures, and includes. Do not trust line numbers from memory.

4. **Adapt source code.** Apply the upstream change, substituting RCPU types and preserving `fCT`. When removing or renaming, also remove call sites (grep for the symbol). Keep changes minimal and faithful.

5. **Adapt unit tests.** Translate upstream test code:
   - `m_rng.rand<T>()` → `static_cast<T>(m_rng.rand32())` or use `InsecureRand32()`/`InsecureRandRange()`/`InsecureRandMoneyAmount()` globals.
   - `BOOST_CHECK_EQUAL(uint256, uint256)` → `BOOST_CHECK(a == b)`; `BOOST_CHECK_NE` → `BOOST_CHECK(a != b)`.
   - Drop any `PayToAnchor` / `TxoutType::ANCHOR` test cases.
   - `std::variant_size_v<CTxDestination>` should be 9 here (not upstream's 10).

6. **Commit.** Each upstream PR becomes one commit. Use the RCPU agent identity:
   ```
   git -c user.name=rcpu-agent -c user.email=agent@rcpu.local commit -m "<convention-scope>: <subject>" -m "<body>"
   ```
   Conventional-commit scopes: `fix`, `perf`, `test`, `refactor`, etc. Subject in lower-case, no trailing period. Reference the upstream PR number in the body.

7. **Push.** Must go through the local SOCKS5 proxy:
   ```
   git -c http.proxy=socks5h://127.0.0.1:10808 push origin main
   ```

8. **Wait for CI.** CI runs `make check` (a scoped unit-test list) plus RCPU-specific functional tests. The user typically uploads a CI screenshot. If it fails, fix and re-push; do not silently leave a red build.

## CI scope notes

- `make check` runs only these suites: `blind_tests, confidential_validation_tests, randomx_tests, dualalgo_tests, logging_tests, coinbase_confidential_tests, group_outputs_tests, pow_tests, argsman_tests, tx_verify_tests, validation_block_tests, serialize_tests`. New tests in `transaction_tests` or `sighash_tests` compile but are **not executed** by CI — coverage is compile-only. `tx_verify_tests` is the one executed suite that exercises real ECDSA signature verification, so sighash changes are indirectly validated there.
- Functional CI runs only RCPU-scoped tests (`wallet_confidential`, `wallet_coinbase_category`, `feature_dualalgo_count_cache`, `feature_rbf_rcpu`, `wallet_bumpfee_ct`). Upstream legacy functional suites are not runnable on RCPU as-is, so reject-reason string changes from upstream PRs generally do not need to be ported to those suites.

## References

- `doc/release-notes.md` — RCPU change log
- `doc/confidential-transactions.md` — CT serialization rules
- `src/script/interpreter.cpp` — sighash and script execution
- `src/validation.cpp` — mempool/block acceptance (CheckInputScripts, PolicyScriptChecks)
- `src/policy/policy.cpp` — IsStandard, AreInputsStandard, IsWitnessStandard
- `src/net_processing.cpp` — peer message handling and punishment
