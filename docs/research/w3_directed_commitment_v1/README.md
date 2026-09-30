# W3 — directed commitment recipe and usable instrument profiles

Implemented locally on `gpt/w3-model-recipes-2026-09-30`, from base
`b118c80e981c08ec6d7f9ab6aacc177979186cf2`. No shared runtime, runner,
ledger, frozen research instrument, STATE, main or integration branch changed.

## Delivered behavior

- `experiment historical` restores the original first-response archive into a
  temporary directory, verifies container and all member hashes, validates
  exact request/source/recipe/provider identities and accounting, and produces
  profiles accepted by the existing `loom.model_profiles/1` consumer. All 96
  original answers are retained; no inference is performed.
- `directed_refute_v3` is a data recipe: only q02's false criterion extends
  active-refutation v2. It explicitly distinguishes A→B from B→A and preserves
  withdrawal of denial across unrelated turns. It does not correct outputs.
- A second agent authored 12 new bilingual cases / 48 queries before inspecting
  implementation: 14 supported, 10 refuted, 24 unknown. These are correlated,
  adversarial DEV cases, not an untouched population holdout.
- `prepare`, `verify`, and `score` bind exact inputs, labels, recipe, dependencies
  and full query inventory. Missing attempts remain unavailable in the full
  denominator; semantic conflicts are separately retained in execution counts.
  `score` saves predictions before loading labels. No CLI command sends a request.

## What was actually measured

| Recipe | Original correct / planned | Known original cost | W3 new calls |
|---|---:|---:|---:|
| historical_refute_v1 | 42/48 | USD 0.001482852 | 0 |
| active_refute_v2 | 45/48 | USD 0.001664292 | 0 |
| directed_refute_v3 | **Unmeasured** | None | 0 |

The first two rows reproduce prior results; their difference is not a new W3
quality gain. All remaining v2 errors are unknown→refuted. Two are reverse-edge
withdrawal mistakes, one revives a withdrawn denial after unrelated speech.
The original audit has the exact queries and probabilities. These observations
motivate v3; they do not prove its effectiveness.

Both profiles are scoped to returned `typesafe/jev-1.13-20260917` / `TypeSafe`,
the supplied-node latest-commitment task, recipe and authored DEV population.
The dated model identifier is not proof of immutable weights. Source entries
are verified as real Git blobs at the base commit, including the original ZIP.
Costs are counted once per request batch. All 96 generation billing audits were
unavailable; provider-reported usage is not an independently verified invoice.
No calibration, global reliability, automatic promotion or production routing
is claimed. Profile generation requires the already used `jsonschema` dependency.

## Current preparation and first artifacts

**Use `prepared_final/`.** It contains three unexecuted 48-request batches
(new recipe on old DEV, and both arms on independently authored DEV) plus the
48-request historical comparator **disabled for live execution**. The existing
baseline uses original saved first responses instead of repeated paid calls.
The three potential batches reserve USD 0.144 together; this is planning,
not spending or renewed authorization. ROOT must check the actual remaining
shared USD2 budget and select which comparison to run. Reusing historical
baseline output leaves a time/provider-state confound; it is not a simultaneous
randomized trial.

`prepared/` is the preserved first preparation, superseded and **not executable
under the current dependency freeze**. During review we made the existing
historical control explicitly replay-only and removed its duplicate live
reservation. No requests from either preparation were sent. The first snapshot
is retained to expose the correction, not offered as another execution option.

`replay_first/` preserves exact first outputs/profiles. Initial local replay
failed because loose `.response.bin` files are intentionally stored inside the
original committed ZIP; the implemented verifier restored those original bytes,
without fabricating responses or overwriting repo files. Earlier preparation
review found an unfrozen label file and an incomplete-freeze acceptance hole;
gold-byte hashing, exact inventories and regenerated input/count checks fixed
them. Independent tests exercise both counterexamples.

## Reproduce

From the repo root, in Python with `jsonschema` installed:

```sh
python -B -m loom.tools.structure.w3_directed_commitment_v1.experiment historical --output /tmp/w3-fresh-replay
python -B -m loom.tools.structure.w3_directed_commitment_v1.experiment verify --plan docs/research/w3_directed_commitment_v1/prepared_final
python -B -m unittest loom.tools.structure.test_w3_directed_commitment_v1 -v
TMPDIR=/var/tmp python -B -m unittest discover -s loom/tools/structure -p 'test_*.py'
python -B -m unittest discover -s loom/tools/eval -p 'test_model_profiles.py'
```

Every output destination must be new. Profiles are tested against the existing
contract without modifications. The root test wrapper joins the existing
`research.structure` discovery gate; no CMake or ABI edits are needed.

After an authorized new first run, use `experiment score --plan ... --population
independent_dev --arm directed_refute_v3 --manifest ... --run-dir ... --output ...`.
Never label scripted responses as live evidence. Hash checks detect drift but
cannot authenticate an attacker who rewrites all source files and hashes;
the committed receipt is the external checkpoint.

## Publication status

Published on `gpt/w3-model-recipes-2026-09-30` at implementation commit
`729699fe8dab6dd696b133bd277d3717c3bbd055`. Its tree
`cda488ba218a6ee8e07da3a5debf6c615d671fd5` matches the tested local checkpoints
exactly. Remote fetch plus an empty diff verified publication.

The initial automatic approval rejection and subsequent terminal credential
failure are historical. The owner renewed authorization; the connected GitHub
then published the same files as one commit. Local checkpoint SHAs in the
receipt remain provenance, not claims that those commits exist remotely.
The separate follow-up documentation commit records this resolution.

ROOT alone reviews/integrates. New-recipe quality is still unmeasured; live
execution awaits ROOT coordination and current shared-budget accounting.
