# First32 executable handoff

Selection and analysis were frozen before reading LIVE responses. This directory
contains the immutable input-only `plan.json`; it is 16 existing T3 relation
cases × meaningful/string, each with expressed/inferred questions: 32 calls,
64 planned decisions, USD 0.032 reservation. The existing dedicated nonresetting
USD 2 key cap remains shared across jobs; the batch ceiling remains USD 0.10.

The adapter's model/provider/price/body guards are independent of the earlier
text-only Jev arm. No earlier arm or question limit was changed. The original
archive is hard-pinned to SHA256
`c14ba47a84192f708fe1fae06454996eb9a90444e42a93cf379b8fb92619673c`.
Actual wire JSON retains the original instruction field ordering and values;
only `provider.max_price` is added. Maximum selected body size is 2,214 bytes.

From repository root, operator preparation/execution is:

```sh
python loom/tools/structure/recipe_live_pilot.py prepare \
  --plan docs/research/jev_recipes_first32_2026-09-30/plan.json \
  --output-dir /workspace/scratch/34e008d7a951/research-recovery/jev-recipes-first32-prepared
python loom/tools/structure/recipe_live_pilot.py preflight \
  --manifest /workspace/scratch/34e008d7a951/research-recovery/jev-recipes-first32-prepared/manifest.json
python loom/tools/structure/recipe_live_pilot.py run \
  --manifest /workspace/scratch/34e008d7a951/research-recovery/jev-recipes-first32-prepared/manifest.json
```

Root supplies `OPENROUTER_KEY_FILE` separately; do not put its contents in commands
or artifacts. Prepare/preflight are GET-only. The CLI fixes the canonical run
directory for this experiment; it cannot be redirected to a new empty ledger.
Inspect JSON `stopped_reason` and ledger counts: the current CLI returns exit 0
even when execution stops. Correct that only in a later version, preserving the
executed source snapshot. Do not edit runner/dependencies after preparation.

After the series closes:

```sh
python loom/tools/structure/recipe_live_score.py \
  --manifest /workspace/scratch/34e008d7a951/research-recovery/jev-recipes-first32-prepared/manifest.json \
  --run-dir loom/tools/structure/.recipe-live/jev-recipes-t3-first32-20260930 \
  --output docs/research/jev_recipes_first32_2026-09-30/first_score.json
python loom/tools/structure/recipe_live_archive.py \
  --prepared-dir /workspace/scratch/34e008d7a951/research-recovery/jev-recipes-first32-prepared \
  --score docs/research/jev_recipes_first32_2026-09-30/first_score.json \
  --output docs/research/jev_recipes_first32_2026-09-30/first_evidence.zip
```

Commit this evidence archive and score before ending the session. The archive
includes exact raw bytes and start/terminal receipts, checkpoint, original
prepared inputs/catalog/manifest, executed code, frozen T3 archive/gold,
inventory hashes and offline replay commands. Locks/credentials are excluded.
It is exclusive: a second invocation cannot overwrite the first evidence.
Restore evidence after a workspace refresh; never recreate it through paid
calls. Offline relocated replay was verified to reproduce an identical report.

## Verification, before LIVE response inspection

- First adapter run: 12/12 tests, 2.223 s, mocked responses only.
- Frozen archive/stop-latch controls: 53/53 combined tests, 3.017 s.
- Contradictory optional audit/cost preservation: 54/54, 3.273 s.
- Scorer denominators/baselines/bootstrap/replay: 59/59, 3.825 s.
- Exact backup and relocated replay: 61/61, 4.522 s.
- Wider `unittest discover -s loom/tools/structure`: 491 tests in 6.150 s;
  479 passed and 12 credential-handoff setup errors. The runtime's `/tmp/.git`
  makes every default synthetic temporary session fail the existing strict
  `outside_git` guard (`private_path_inside_git`). These are environment/setup
  failures, not relaxed or hidden gates. A rerun with an allowed temporary
  directory outside Git remains required for a fully green discovery claim.
- Independent read-only audit: 15 adapter tests plus six separate failure
  injections, each one POST then offline stopped resume; no remaining paid
  blocker identified. Missing usage, wrong identity, boolean probability,
  missing output tokens and detected BYOK stop without retry.

All these are mechanism tests, not model-quality measurements. No LIVE calls,
credentials or new independent validation labels were read by the adapter
implementer during this preparation. Source-attribution and controlled
counterfactual queries remain a subsequent preregistered panel; first32 cannot
claim to measure queries absent from its frozen bodies. Authored T3 validation
is already-known synthetic development, not blind holdout.
