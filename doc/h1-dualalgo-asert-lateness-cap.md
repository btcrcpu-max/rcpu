# H-1 design: bounding per-algorithm ASERT lateness in the dual-algo schedule

Status: **implemented in `audit/2026-11-16-fixes`** (Option B), with activation
height and constants left as release-time decisions. This document remains the
design/review reference; see section 9 for the implementation mapping.
Audience: node maintainers, pool operators, release manager.
Related audit: `RCPU-AUDIT-2026-11-16.md` finding H-1.

## 1. Problem

`GetDualAlgoWorkRequired()` (`src/pow.cpp`) runs one absolute-scheduled ASERT
instance per proof-of-work algorithm:

```
nTimeDiff   = pindexLast->GetBlockTime() - pindexAnchor->GetBlockTime()
nHeightDiff = number of blocks of THIS algorithm since activation
target      = refTarget * 2^((nTimeDiff - spacing*(nHeightDiff+1)) / halfLife)
```

`nTimeDiff` is measured against the last block of *either* algorithm, while
`nHeightDiff` counts only the algorithm being evaluated. If the bit-30
algorithm (Etchash on mainnet) is not mined for `T` seconds while RandomX keeps
the chain moving, `time_delta` grows without bound and `CalculateASERT()` clamps
the target to `powLimit`. Every subsequent Etchash block consumes only
`spacing = 600 s` of that banked lateness, so the miner gets approximately
`T / 600` blocks at `powLimit` difficulty, each paying the full height-based
subsidy.

This is not a chain stall: the chain is healthy, only one of the two algorithms
is idle. It is therefore a structural property of "free competition with
independent difficulty", not an anomaly that ASERT was designed to absorb.

Quantitatively (mainnet, 2026-11-16): `powLimit = 0x00003ffffc...`; the height
21000 `nMinimumChainWork` implies an average block work of about `2^23.9`,
i.e. a target about 60x harder than `powLimit`. With a 2-day half-life,
roughly 12 days of Etchash dormancy is enough to reach `powLimit`. Activation
was 2026-11-02, so a 14-day dormancy would already have banked about 2,000
minimum-difficulty blocks (~10.08M RCPU at 5,000 RCPU/block).

Consensus validation cannot catch this: `GetNextWorkRequired()` itself returns
`powLimit`, so `ContextualCheckBlockHeader()` sees a perfectly matching
`nBits`.

## 2. Goals and non-goals

Goals:

- Bound the number of blocks an algorithm can mine at a difficulty that is far
  below its last honest difficulty, without a coordinated chain stall.
- Keep the pre-activation rules byte-for-byte identical.
- Keep the fix deterministic across restarts, reorgs and `-reindex`.
- Preserve the ability of an under-mined algorithm to recover to a profitable
  difficulty.

Non-goals:

- Reversing subsidies already paid to low-difficulty blocks (separate incident
  response if a burst already happened).
- Changing RandomX or Etchash primitives, the 112-byte header, or the
  activation height of the dual-algo schedule.
- Making the two algorithms produce equal block counts; "free competition"
  remains.

## 3. Options

### Option A — hard lateness cap (minimal, recommended interim)

Clamp the per-algorithm lateness before feeding it to `CalculateASERT()`:

```
int64_t nTimeDiff = pindexLast->GetBlockTime() - pindexAnchor->GetBlockTime();
if (nTimeDiff > params.nDualAlgoMaxLateness) {
    nTimeDiff = params.nDualAlgoMaxLateness;
}
```

With `nDualAlgoMaxLateness = L`, the target at the cap is
`refTarget * 2^(L/halfLife)` and at most `L/600` blocks can be mined before
the ideal schedule catches up.

Properties:

- One-line consensus change; easy to reason about and test.
- Bounds the burst to `L/600` blocks per dormancy.
- Also bounds how much easier the algorithm can become, so recovery is weaker
  for an algorithm that is more than `2^(L/halfLife)` unprofitable.

Suggested starting value: `L = 24 h` (144 blocks, at most `2^(24h/48h) = 1.41x`
easier). A larger `L` improves recovery but linearly increases the worst-case
burst; `L = 48 h` gives 288 blocks and a 2x drop.

### Option B — re-anchor on resume (better recovery, more code)

Store the last block time of each algorithm in `CBlockIndex`
(`nLastRandomXTime`, `nLastEtchashTime`), and when the algorithm resumes after
a gap larger than `nDualAlgoReanchorGap`:

1. reset the ASERT anchor for that algorithm to its last block (height, nBits,
   time);
2. start `nHeightDiff` at 0;
3. apply a one-time maximum drop of `nDualAlgoMaxDrop` (for example 8x) to
   `refTarget`;
4. then run the normal ASERT formula from the new anchor.

This avoids "banking" the gap at all: the algorithm gets one bounded step down
when it resumes, then normal per-block adaptation. It recovers better than
Option A because the difficulty can keep falling over subsequent blocks
without an unbounded pre-paid burst, and it does not require a global time
cap.

Cost: two new `CBlockIndex` fields (must be rebuilt on load, like
`nSecondAlgoCount`), a new re-anchor condition, and more test vectors.

### Option C — work-relative floor (defense in depth)

Clamp the algorithm's target so that its expected work is at least `F` times
the other algorithm's recent work (for example `F = 1/16`). This prevents one
algorithm from ever becoming trivially cheap, independent of elapsed time.
It is harder to specify deterministically (which window? how to handle
reorgs?) and is not recommended as the first fix.

## 4. Recommendation

For the next consensus release, implement **Option B**, with **Option A** as a
fallback if the release window is too short. Option B directly removes the
banking mechanism and preserves recovery; Option A is a one-line mitigation
that caps the damage but may leave an unprofitable algorithm stuck at the cap.

If Option A is chosen first, it should still be replaced by B later; do not
ship both mechanisms at different heights without a clear migration.

## 5. Activation

Add a new consensus parameter:

```
int nDualAlgoLatenessFixHeight;   // INT_MAX = never (default for non-RCPU)
```

- Mainnet/testnet: `TIP + 2016` at release time, recomputed on the release
  branch; do not reuse the `27326` CT height.
- Regtest: `0` (immediate), so functional tests can exercise the new rule.
- Other chains: `INT_MAX`, preserving upstream behavior.

Before the activation height, `GetDualAlgoWorkRequired()` must execute the
current formula unchanged. At and after it, apply the chosen rule. This is a
hard fork: old nodes will keep using the old rule and can diverge if a
low-difficulty event occurs after activation. Announce the height and require
all nodes/pools to upgrade before it.

Recommended rollout:

1. merge the code and tests with `nDualAlgoLatenessFixHeight = INT_MAX`;
2. run a mainnet header replay (21000..tip) and compare `nBits` before/after
   the new rule; publish the number of headers whose `nBits` changes;
3. pick the activation height from the replay and a fresh `getblockcount`;
4. tag, publish, announce; monitor the first 2,016 blocks after activation.

## 6. Test plan

Implemented in `src/test/dualalgo_tests.cpp`:

- `dual_algo_reanchor_discards_banked_lateness`: an algorithm that mined once at
  activation and then went idle for 60 days is capped by `nDualAlgoMaxDrop`, and
  the resume block becomes its new anchor;
- `dual_algo_never_mined_first_block_is_bounded` (POW-02): Etchash never mines.
  The old rule releases the full lateness as `powLimit`; the new rule caps the
  first block at `nDualAlgoMaxDrop` above the activation reference, anchors on
  it, and the second block no longer falls back to `powLimit`;
- `dual_algo_never_mined_randomx_mirror`: the same scenario with a never-mined
  RandomX;
- `dual_algo_never_mined_above_threshold_uses_full_gap`: 13h of lateness must
  feed the FULL gap into ASERT and must not be capped. This rejects the
  `min(gap, 12h)` shortcut, which would report a ~1.19x change;
- `dual_algo_never_mined_ignored_without_retargeting`: chains without ASERT
  (`fPowNoRetargeting`) are unchanged.

Still required before activation:

- a functional test driving the resume path over RPC (`getblocktemplate` with
  `{"algo":"etchash"}` on regtest, fix height 0, activation 150);
- a reorg across the fix height and a `LoadBlockIndex` restart comparison
  against a fresh sync;
- the mainnet header replay described below.

Mainnet replay:

- run `contrib/devtools/dual_algo_replay.py --never-mined-fix-height <H2>`
  against a synced node and publish the number of headers whose `nBits`
  changes, plus the count of never-mined resumes.

## 7. Operational checks before activation

1. `getblocktemplate '{"rules":["segwit"],"algo":"etchash"}'` on a synced
   node: if `target == powLimit`, the old-rule credit already exists; the fix
   will discard it at activation, which may make the first Etchash blocks
   harder/slower. Announce this.
2. Count bit-30 blocks since 21000 and their `nBits` distribution; if a burst
   already happened, treat it as an incident (subsidy concentration), not just
   a code fix.
3. Confirm pools/miners understand that both algorithms remain free
   competition; the fix only caps the *drop*, not the choice of algorithm.

## 8. Open questions

- Should the cap apply to RandomX too, or only to the second algorithm? The
  same mechanism can strand RandomX if Etchash dominates; applying it to both
  is simpler and symmetric.
- Should `nDualAlgoMaxLateness`/`nDualAlgoMaxDrop` be consensus constants or
  configurable? Consensus constants; making them configurable invites splits.
- Do we need a one-time "credit reset" at activation, or does the new formula
  naturally discard the old credit? With Option A/B, the new formula uses the
  capped/anchored value, so old credit is discarded by construction; document
  this explicitly.

## 9. Implementation mapping (this branch)

Consensus parameters (`src/consensus/params.h`):

- `nDualAlgoLatenessFixHeight` — activation height. Mainnet and testnet ship
  `29909`; regtest is `0` so tests exercise the rule. This height is already
  scheduled and must not be moved again.
- `nDualAlgoNeverMinedFixHeight` — POW-02 resume height for an algorithm that
  has never produced a block. `INT_MAX` means "not scheduled"; the release that
  ships it must set `TIP + 2016` on mainnet and testnet. Regtest is `0`.
- `nDualAlgoReanchorGap` — 12h on mainnet/testnet, 1h on regtest.
- `nDualAlgoMaxDrop` — 8 on all RCPU chains.

Per-index state (`src/chain.h`, memory only, rebuilt like `nSecondAlgoCount`):

- `nLastRandomXHeight`, `nLastEtchashHeight`;
- `nAsertAnchorRandomXHeight`, `nAsertAnchorEtchashHeight`.

New code:

- `UpdateDualAlgoState()` (`src/pow.cpp`) is called from
  `BlockManager::AddToBlockIndex()` and rebuilt in `BlockManager::LoadBlockIndex()`.
  It records the last block of each algorithm and moves the anchor to the
  resume block when the gap exceeds `nDualAlgoReanchorGap`.
- `CumulativeAlgoCount()` derives the per-algorithm count since an arbitrary
  anchor from `nSecondAlgoCount` (and walks the chain when the cached count is
  unavailable).
- `GetDualAlgoWorkRequired()`:
  - before the fix height, or when the gap is below the threshold, uses the
    stored anchor and the original formula;
  - on a re-anchor, uses the last same-algorithm block as `refTarget`, sets
    `nHeightDiff = 0`, and caps the one-time target increase at
    `nDualAlgoMaxDrop` (also clamped to `powLimit`);
  - the resume block becomes the next anchor, so the old gap is not carried
    forward.

Regression test: `dualalgo_tests/dual_algo_reanchor_discards_banked_lateness`
builds a synthetic chain with one Etchash block at activation, a 60-day idle
gap, and asserts: old rule → `powLimit`; new rule → exactly `8x` the last
Etchash target; next block → anchored on the resume block, not `powLimit`.

The placeholder activation height and constants must be reviewed against a
mainnet header replay before tagging; this is a hard fork.
