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
