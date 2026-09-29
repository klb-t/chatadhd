# Refinement v1 — T2 (2026-09-29)

[U] Implement corpus + reference evaluator requested in
`docs/GPT_OFFLINE_TASKS_2026-09-29.md`, T2 (R34).
[P] Synthetic author-written material and a narrow, transparent scoring protocol.
Not a consolidation algorithm, native integration or model-quality result.

## Corpus

40 newly written fictional blocks: 20 PL / 20 EN; 10 each for letters, code,
text editing and analysis. 125 turns include 85 user turns. Four blocks contain
parallel tasks, so there are **44 gold ActiveTaskSpec projections**. Their
137 manually authored clauses include 110 active/contested clauses, 8 exceptions
and 7 open issues. All eight requested turn-act labels occur.

`dev.jsonl`: 20 blocks (9 PL / 11 EN), 22 task specifications.
`validation.jsonl`: 20 blocks (11 PL / 9 EN), 22 task specifications.
The validation half is frozen and opt-in, **not blind or independently authored**:
ChatGPT authored both splits and the evaluator. No source conversation or prior
Jev text was copied, and the cases are not PL/EN translation pairs. Real-world
and independently authored follow-up validation are still required.

The `tasks[].gold` records are compact manually written specifications: each
clause fixes text, kind, status, source-turn indexes (zero based), conditions and
superseded clause ids. `refinement_fixture.py` mechanically supplies the T1
ActiveTaskSpec envelope and UTF-8 source map. This avoids storing boilerplate
repeatedly; **it performs no semantic extraction or labeling**. `--mode gold`
emits each full gold ActiveTaskSpec, history events and labels as ready JSONL.
Names/fields here are fixture notation, not a second production knowledge store.

`history_source` contains only the fictional unannotated source. Its locator id
hashes that object's canonical UTF-8 JSON, not the outer JSONL file or the gold.
Times beginning 2025-01-01 are declared synthetic ordering metadata, not real
historical event times. The exporter preserves unknown model/source limitations
rather than pretending these are provider exports.

## Freeze

Plan: [PLAN.md](PLAN.md), recorded before scoring. Manifest freezes source files,
plan and full mechanically expanded gold. SHA-256:

- dev: `01eba7493c631a4e54e5c009d17690bf231b4c2aa5caf4680275af235906511c`
- validation: `6da59f0d216e63f5dacb24b318576afc9964bd1591ee4f90f34e89142b3342f0`

The initial materializer shared mutable act lists with its source fixture. A
mutation test caught that; the output now copies these lists. The manifest records
old/new materializer hashes and the reason. **Neither corpus nor expanded-gold
hash changed.** No model output was observed before or after this correction.

## Use from repository root

Dependencies are those of T1: `loom/tools/contracts/requirements.txt`.

```sh
python loom/tools/eval/refinement_fixture.py loom/tests/fixtures/eval/refinement_v1/dev.jsonl
python loom/tools/eval/refinement_fixture.py loom/tests/fixtures/eval/refinement_v1/dev.jsonl --mode inputs > /tmp/refinement_inputs.jsonl
python loom/tools/eval/refinement_fixture.py loom/tests/fixtures/eval/refinement_v1/dev.jsonl --mode gold > /tmp/refinement_gold.jsonl
python -m unittest discover -s loom/tools/eval -p test_refinement_eval.py -v
python loom/tools/eval/refinement_eval.py loom/tests/fixtures/eval/refinement_v1/dev.jsonl predictions.json --output /tmp/refinement_report.json
```

`predictions.json` is `{"predictions":[{"case_id":"d01","specs":[...],"turn_labels":[...]}]}`.
`turn_labels` is optional; absent means unavailable metrics, not a perfect score.
Supply the actual outputs, not a copy of gold, for any model evaluation.
Validation scoring additionally needs `--include-validation`.

Input-only export removes gold, act labels, scenario tags and task annotations.
A future runner must retain source/case identities. Where its task names differ,
an explicit reviewed mapping to the fixture task ids is required before scoring;
the evaluator does not solve semantic task identity. Do not feed gold task
boundaries to the model and describe that as task-discovery accuracy.

Clause alignment is exact whole text (NFC and whitespace normalization only),
or an explicitly reviewed one-to-one mapping:

```json
{"alignment":{"reviewer":"reviewer-id","method":"manual semantic comparison",
 "pairs":[{"task_id":"main","candidate_id":"output-clause","gold_id":"g"}]}}
```

This is an evaluator-side attestation, never sent to the generating model. The
scorer checks references/conflicts but does not certify the reviewer. Combined
multi-clause paraphrases must be atomized or reviewed separately; no hidden
LLM judge, lexical-overlap entailment or confidence-to-truth conversion exists.

## What a verdict means

`pass`: conforms to this reference representation and its structural guards.
`needs_review`: unfamiliar wording, source alignment or nonreference instruction
rendering prevents an automatic verdict. `fail`: a stated contract/reference
check fails. Findings give codes and ids, not confidential payload values.

Reference coverage reports numerator/denominator; it is not general semantic
recall. New arbitrary positive instructions supported only by a question or a
content-free complaint are flagged. The scorer checks exception conditions,
status/supersession, executor-only context, unresolved conflicts, source quotes,
source locators, task boundaries and instruction/spec divergence. It cannot
certify every semantically equivalent paraphrase or every possible hallucination.

## Executed local checks

**81/81 unittest methods passed**: 40 gold self-consistency checks plus 41 targeted
mechanism/protocol/mutation checks. Includes the mutable-label regression fix.
All 44 expanded specifications pass the T1 validator. These are author-created
fixtures and manipulated outputs, **not 40 successful model consolidations**.
No live model, CTest, C++, Android, secret, Actions or production threshold changes.
