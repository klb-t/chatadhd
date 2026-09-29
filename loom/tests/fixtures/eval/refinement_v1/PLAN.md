# T2 — protocol before scoring (2026-09-29)

[U] Source: docs/GPT_OFFLINE_TASKS_2026-09-29.md, T2; R34 and conceptual §11.5.
[P] This is a reference evaluation protocol, not a semantic consolidator.

## Units and freeze

40 newly authored fictional PL/EN blocks, 20 development / 20 validation.
Each block has source turns, per-user-turn act labels and manually written gold
clauses. Parallel tasks have separate gold specifications. Cases are not copied
from the 64 Jev texts and are not translation pairs. They are authored by the same
ChatGPT session as these tools, NOT an independent human panel or blind holdout.
Freeze both source files and the exact expanded-gold hashes before writing the
scorer. Validation is fixed and opt-in; never tune a consolidator on it and still
call it unseen. Reading it for grammar/self-consistency is not model validation.
The two separately prohibited repository holdout branches remain untouched.

Gold clauses are a compact lossless notation of ActiveTaskSpec: kind, status,
text, source-turn indexes, conditions and supersedes. The mechanical materializer
adds declared synthetic event times, source pointers, product/goal ids and a
reference instruction renderer. It generates NO semantic labels or new clauses.
Its hash is frozen too. `--mode gold` emits full T1 ActiveTaskSpec objects;
`--mode inputs` emits only text/roles, not labels, gold, tags or task boundaries.
Use the latter as the input to any future consolidator.

## Planned observations (none collected yet)

Prediction envelope: case_id, specs (T1 ActiveTaskSpec array), optional turn_labels,
optional alignment. A case is the unit of analysis, not each sentence or retry.
Record actual model/version/recipe in a future run manifest; absent model output
is not a success. Missing cases are counted separately and cannot improve scores.

The reference scorer aligns exact NFC/whitespace-normalized whole clause texts,
not keywords/substrings/embeddings. It can accept separately reviewed one-to-one
semantic alignments; reviewer identity and method must be recorded. Matching text
alone does not establish correct status, kind, source, scope or exception limits.
An unfamiliar paraphrase is `needs_review`, NOT automatically a hallucination.
Do not present exact-reference recall as general semantic accuracy.

Report by case and domain/language: reference-clause coverage with numerator and
denominator; expected exceptions retained; rejected/superseded clauses improperly
reactivated; contested requirements unilaterally resolved; source-map/scope errors;
executor-only material turned into product requirements; new positive instructions
supported solely by question/dislike-without-delta turns. Unknown clauses require
review, not a guessed correctness score. Comparisons of conditions/status are
reference-conformance checks; manual adjudication may recognize equivalences.

The instruction text must either match the deterministic reference renderer of
its supplied active clauses or be separately reviewed; schema-valid spans do not
prove that prose follows the clauses. No bag-of-words entailment checker.
For optional act predictions use sets of (event_id, task_id, act); precision/recall
and exact turn-label agreement are separate from specification coverage. With no
act predictions, these metrics are unavailable, not zero or perfect.

## Controls and acceptance

Use the gold itself only as a self-consistency check of the evaluator. Deliberate
mutations exercise exception removal, rejected-variant return, invented fixes,
context leakage, unresolved conflict, task mixing, source tampering, future data,
paraphrase/review handling and instruction/spec divergence. Perfect performance
on these authored assertions is NOT a consolidator performance measurement.

Later compare raw clarification history, ordinary summary, consolidated current
specification and consolidation with selected evidence. Keep the same final task,
model and evaluation rubric. Separate semantic fidelity from token savings and
avoid rewarding shorter instructions that omit exceptions. No live requests,
paid runs, native promotion or production thresholds are part of this package.
