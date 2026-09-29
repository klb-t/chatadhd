# GPT offline work log — 2026-09-29

Base: `4508af5a67c5185d578ce81722bcbe9983cb066e`.
Working branch: `gpt/offline-2026-09-29`.
Source of work: `docs/GPT_OFFLINE_TASKS_2026-09-29.md`.

## T1 — contracts / reference validation

[U] Deliver four draft-2020-12 contracts plus examples and offline tests for
R26/R30/R34/R38, without a second knowledge store.

[P] Added five schemas (four contracts + common native-reference definitions),
`docs/contracts/README.md`, 13 positive examples and 28 declared negative mutations,
`loom/tools/contracts/validate.py`, `test_contracts.py`, and local requirements.

Executed locally:
`PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s loom/tools/contracts -p 'test_*.py' -v`
— **74/74 test methods pass**. Python 3.13.5; jsonschema 4.26.0; referencing 0.37.0.
Source packet examples and tests are authored fixtures, not measured model output.

Limitations: schema/relational checks only; no C++ integration, CTest, Android,
source-byte verification or model/semantic-quality measurement. Full repository
build not attempted; local workspace contains only the new deliverables.
No paid/model calls, Actions or secret access; prohibited work branches unread;
production source files and STATE.md unchanged. T2–T8 remain pending here.

## T2 — frozen refinement corpus / reference evaluator

T1 commit: `866fa55067db07f3001a63a900dbeff7e0447f06` (12 additions; no existing
production files changed). T1 rerun: **74/74**, 5.305 s locally.

[U] T2: >=40 PL/EN refinement blocks, explicit gold, act labels, frozen validation
half and checks for lost exceptions, rejected variants and invented corrections.

[P] Added `loom/tests/fixtures/eval/refinement_v1/` (dev/validation JSONL,
manifest, PLAN, README), `loom/tools/eval/refinement_fixture.py`,
`refinement_eval.py`, `test_refinement_eval.py`.
40 synthetic blocks, 20 PL/20 EN, 4 domains, 125 turns (85 user), 44 manually
authored gold task specifications in compact clause form. The mechanical exporter
emits full T1 ActiveTaskSpec projections; the exporter performs no inference.
20 validation blocks frozen with SHA-256. Same author as dev/tools, not a blind
or independent human test. Prohibited existing holdout branches remain unread.

Executed:
`PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s loom/tools/eval -p test_refinement_eval.py -v`
— **81/81 methods passed**, 7.073 s: 40 gold self-consistency checks and 41
mechanism/protocol/mutation checks. All 44 gold specs validate against T1.
The first run caught mutable act-list sharing between materializer output and
fixture; fixed by copying, with a new regression test. Manifest amendment records
the materializer hashes; frozen corpus and expanded-gold hashes unchanged.

[P] Exact-reference/structural evaluation plus recorded manual clause alignments;
unfamiliar paraphrases produce needs_review, not an invented semantic verdict.
Missing predictions remain in denominators; semantic_accuracy is unavailable.
No real consolidation/model output measured, no native integration/build tested.
No secrets/paid calls/Actions; only new files and this log on the offline branch.
T3–T8 remain pending; this is a two-task increment, not completion of the task list.

## T3 — Jev recipes prepared, not executed (second offline increment)

Input baseline: `494b164c4515b875014b3cfd1c3760de23bda401`.
The latest task note on Claude's line was read, including T9–T13; the prescribed
next unfinished work was T3/T4. Intended integration branch remains
`gpt/offline-2026-09-29`. This session's GitHub connector exposes reads but no
write actions. **No remote commit or branch update was made for this increment.**
Local files and a binary Git patch are supplied for Claude to apply; do not treat
this entry as evidence that the increment is already published on GitHub.

[U] T3: new PL/EN corpus and frozen query recipes, no Jev calls.
[P/H] Added `loom/tests/fixtures/eval/jev_recipes_v1/`,
`loom/tools/eval/jev_recipes.py`, `test_jev_recipes.py`, and
`docs/research/JEV_RECIPES_PLAN_2026-09-29.md`.

36 authored fictional cases (18 PL/18 EN), 18 development/18 validation;
16 relation, 12 context, 8 routing. Same assistant authored texts/labels/code;
not a blind independently judged holdout. Source freeze predates model outputs
(none exist); an amendment records a packaging-only plan clarification.
Four instruction variants preserve state and instruction values. Subgraph
relevance uses independent binary questions. Actual detail representations are
supplied; omit and unavailable are distinct.

208 prepared body JSONs in a solid-compressed interchange ZIP, with exact body
exporter and a hash/routing index. Both hierarchy children are prepared but only
the root-selected one may be run: max **184 calls**, no retry, if all planned
flat/hierarchical comparisons succeed. No authorised amount, executor, model
response or analysis result is implied. Adding 4/8/16 options tests distractors,
not balanced 16-category classification.

Executed locally: **30/30 unittest methods**, 1.366 s in the first recorded run.
Additionally all **208/208 bodies** passed the supplied Jev Lab request validator
(`protocol.py` SHA-256 `de0b83da481f14aeb7a4073438f1515c582ebb7615e7ce02a22a70478489addb`).
This verifies local format compatibility, not the live API. No new response or
statistical quality result was generated.

## T4 — retrospective conditional model profiles

[P] Added `docs/contracts/model_profile.schema.json`,
`loom/tests/fixtures/model_profiles_v1.json`,
`loom/tests/fixtures/model_profiles_v1/source_extract.json`,
`loom/tools/eval/model_profiles.py`, `test_model_profiles.py`, and
`docs/research/MODEL_PROFILES_2026-09-29.md`.

14 profiles: each Jev question q01–q12 separately, plus two native graph
serialization/grounding instruments. Source extract is explicitly transcribed
selected aggregates/reports, not a copy of raw responses. Source paths, git blob
identities and pointers/sections are recorded. Full-repository verification mode
checks original blobs and extracted Jev metrics/question instructions; **not run
against originals here**, since this checkout contains only the offline additions.

Conditional semantic accuracy of native outputs is unavailable (zero graphs
passed); missing/uncertain attempts are not labelled semantic failures. Costs
stay at their actually reported batch/model granularity. q01 high-score errors,
unknown positive denominators, lack of calibration certification and temporal
validity are explicit. Native software test counts are separate evidence, not
model reliability. Owner's philosophy observation remains nonnumeric feedback.
The compensating instruction has an unexecuted ablation plan with control arms,
no new ablation dataset/outputs or enabled promotion.

Executed locally: **35/35 unittest methods**, 4.445 s in the first recorded run.
Old T1/T2 rerun: **74/74** (5.041 s), **81/81** (8.286 s).
Total current local reference/mechanism checks: **220/220**. Not CTest, model
accuracy, browser, Android or native integration. No production changes, keys,
Actions or paid requests. T5–T12 remain pending; T13 handoff is below.

## Do sprawdzenia przez Claude'a w natywnym buildzie (T13)

Apply the supplied binary patch to `gpt/offline-2026-09-29` after checking against
base `494b164`; do not overwrite newer independent edits. The patch adds T3/T4
files and appends this log only. No STATE/task-list/protected production file is
changed. Commit verified changes with `[skip ci]`; organisation/merge remain yours.

From repo root:

```sh
python -m unittest discover -s loom/tools/contracts -p 'test_*.py' -v
python -m unittest discover -s loom/tools/eval -p test_refinement_eval.py -v
python -m unittest discover -s loom/tools/eval -p test_jev_recipes.py -v
python -m unittest discover -s loom/tools/eval -p test_model_profiles.py -v
python loom/tools/eval/jev_recipes.py
python loom/tools/eval/model_profiles.py
python loom/tools/eval/model_profiles.py --verify-repo-sources
python loom/tools/eval/jev_recipes.py --export-to /tmp/jev-recipes-v1-review
```

Expected: 74 + 81 + 30 + 35 test methods pass; request verifier reports
208 prepared bodies, zero calls; profile verifier reports 14 profiles. Full-source
verification should pass only with the pinned unchanged originals. A missing or
changed source is a reported failure, not permission to rewrite the recorded
results. Export requires a fresh directory, containing 208 raw-body files and the
index. Reimport representative body JSON into Jev Lab; compare exact hash/state
before any separately authorised run. Do NOT run all hierarchy children.

If integrating with the native tree, run its normal offline build/test commands:

```sh
cd loom
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Expected: no new native regression relative to Claude's verified current baseline.
These additive Python fixtures/docs do not claim to fix the existing catalogue
recall gate or satisfy every runtime R37/R38 acceptance criterion. Native source
verification, CTest and canonical STATE update belong to Claude after review.

## T5 — composable workspace reference (third offline increment)

Base: `f9d1168ac29acb91c10b2efb53520a306ca90de7`, the current Claude-line head
read before this work, NOT the old offline branch. Latest task-note instructions
read, including the evening update. T3's newer 208-body files and T4 are now
present on that head; this increment does not overwrite them or duplicate work.

[U] T5 / R29–R31: container/view UI-IR, directed scoped parameter couplings,
detach/freeze/cycle protection, multiple graph/table/card views, detachable presets.
[P] Added `docs/contracts/workspace.schema.json`, `WORKSPACE.md`,
`examples/workspace_v1.json`, `examples/workspace_presets_v1.json`,
`loom/tools/contracts/workspace_ref.py` and `test_workspace_ref.py`.
No native knowledge store, renderer or provider/tool execution is added.

Executed locally: **77/77** T5 unittest methods pass (6.071 s in the recorded run).
The fixture composes five graphs, a table and cards. The propagation policy is
atomic: equal proposals coalesce, unequal competing/cyclic values abort without
partial mutations; frozen containers affect descendants, reconnect/thaw needs
explicit resync. The interface-only preset leaves all other profiles unchanged.
Capability declarations are not verified tool availability. The event-id ledger
is session-local, not a persistent execution log.

T1/T2/T3/T4 regression runs: **74/74, 81/81, 30/30, 35/35**, respectively;
**297/297** reference/mechanism test methods including T5. Restored partial
workspace from prior attachments; compared current source blob ids where read.
Not a full current-repository build/test run or a model quality measurement.
No APIs, secrets, Actions, native CTest, browser or Android exercised. STATE,
quality thresholds and protected production files unchanged. T6–T12 remain open.

### Do sprawdzenia przez Claude'a w natywnym buildzie — T5 / T13

Integrate this small increment on top of base `f9d1168`, preserving intervening
edits; commits use `[skip ci]`. New files only, plus this append to the log.
From repo root:

```sh
python -m unittest discover -s loom/tools/contracts -p test_workspace_ref.py -v
python -m unittest discover -s loom/tools/contracts -p test_contracts.py -v
python -m unittest discover -s loom/tools/eval -p test_refinement_eval.py -v
python -m unittest discover -s loom/tools/eval -p test_jev_recipes.py -v
python -m unittest discover -s loom/tools/eval -p test_model_profiles.py -v
python loom/tools/contracts/workspace_ref.py --demo > /tmp/loom-workspace-demo.json
```

Expected: **77 + 74 + 81 + 30 + 35 = 297 passing methods**. Demo events commit
7 then 2 parameter changes; reference.selection stays `none`, while history and
architecture.depth become 3. No model, tool or canonical graph operation occurs.
Then run the regular native build/CTest ratchet if integrating any native adapter;
this Python-only increment does not claim to repair the catalogue gate. Actual
browser acceptance #5/#6 and C++ numerical parity still need native/E2E work.
