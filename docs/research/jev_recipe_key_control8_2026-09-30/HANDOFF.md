# Optional eight-call diagnostic: executable, not run

Frozen input-only plan: `plan.json`. Protocol:
`docs/research/JEV_RECIPE_KEY_CONTROL_2026-09-30.md`. Four cases selected after
first32 results (r06/r13/r15/r16), two original remaining T3 object arms
(neutral/nonsense), eight distinct unexecuted IDs, sixteen new Noul decisions.
This is targeted development. All four expressed labels are negative: the
all-negative baseline already achieves 4/4, with no positive recall denominator.
Inferred has two positives/two negatives. Preserve every case and baseline.

The adapter reuses the unchanged frozen v1 execution function in a private
binding. Imported v1 module globals, first32 whitelist, original code and
artifacts remain unchanged. Manifest hard-pins all four original v1 code hashes,
the new adapter and existing scorer. Only instruction key names differ among
the three object arms; state, value sequence, criteria and original body rule
order are verified equal. The earlier string is contextual, not a key-only
control. No API calls or real credential reads occurred during preparation.

Eight × USD 0.001 = USD 0.008 reservation, unchanged USD 0.10 batch cap, existing
shared USD 2 dedicated nonresetting key ceiling. Root may run after the graph
panel, using its already-configured `OPENROUTER_KEY_FILE`:

```sh
python loom/tools/structure/recipe_key_control_pilot.py prepare \
  --plan docs/research/jev_recipe_key_control8_2026-09-30/plan.json \
  --output-dir /workspace/scratch/34e008d7a951/research-recovery/jev-recipe-key-control8-prepared
python loom/tools/structure/recipe_key_control_pilot.py preflight \
  --manifest /workspace/scratch/34e008d7a951/research-recovery/jev-recipe-key-control8-prepared/manifest.json
python loom/tools/structure/recipe_key_control_pilot.py run \
  --manifest /workspace/scratch/34e008d7a951/research-recovery/jev-recipe-key-control8-prepared/manifest.json
```

Canonical run location is fixed in the new CLI. Current key GET precedes each
first POST. Known costs survive rejected answers; unknown/interrupted first
attempts stop without paid retry. First start/raw/terminal evidence is exclusive.
This new CLI returns nonzero for a stopped series, without editing original v1.
Do not change adapter/dependencies after preparing the manifest.

After root closes the series:

```sh
python loom/tools/structure/recipe_key_control_pilot.py score \
  --manifest /workspace/scratch/34e008d7a951/research-recovery/jev-recipe-key-control8-prepared/manifest.json \
  --run-dir loom/tools/structure/.recipe-live/jev-recipes-key-control8-20260930 \
  --output docs/research/jev_recipe_key_control8_2026-09-30/first_score.json
python loom/tools/structure/recipe_key_control_pilot.py archive \
  --prepared-dir /workspace/scratch/34e008d7a951/research-recovery/jev-recipe-key-control8-prepared \
  --score docs/research/jev_recipe_key_control8_2026-09-30/first_score.json \
  --output docs/research/jev_recipe_key_control8_2026-09-30/first_evidence.zip
```

Score retains 16 new planned decisions and the 16 corresponding first32
comparators, profiles for four arms on the same four cases, TP/TN/FP/FN,
denominators/baselines and all 32 case/task contrasts. The first32 comparator is
hard-pinned to its original SHA256, not regenerated. Archive includes exact
responses/receipts/checkpoint/prepared state, frozen executed code, T3 inputs/gold
and the immutable earlier comparator, plus SHA256 inventory and offline replay.
Commit first evidence before session end; restore receipts instead of requerying
after a workspace refresh.

Verification before any new outcome: 11 new mocked tests and **72/72 combined**
tests passed in 6.473 s. Tests cover exact values/order and disjoint IDs, original
v1 globals and plan unchanged, provider/model/reservation/global cap rejection,
unknown/interrupted/no-retry resume, immutable stop restoration, zero-positive
denominators, simpler-baseline win, fixed new CLI and exact relocated offline
archive replay. No original code/index/gold/prompt was edited.
