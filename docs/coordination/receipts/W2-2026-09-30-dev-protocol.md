# W2 native retrieval — synthetic DEV protocol v1

Authored on 2026-09-30 **before the first execution of this fixture**. The
fixture, queries, relevance labels and metric definitions below are fixed for
the first run. This is a deliberately small mechanism diagnostic, not independent
validation, held-out data, a tuned acceptance threshold or a generalization claim.
The author inspected the public channel contract and existing implementation,
but did not run these cases while designing the labels. No source archive or
sealed corpus is involved; all text is newly authored synthetic material.

## Fixed corpus and labels

All seven records are native `Claim` objects with observed/archive assessment,
confidence 1, the same date, and synthetic support provenance. Two records have
subject `node alpha`; the other five have separate disconnected subjects. A
second query anchor `node omega` has no claims. There are no decisions, status
records, principles, dependency links or implicit relevance labels. Subjects and
predicate are intentionally neutral. IDs below are fixture IDs, not extracted
claims. Evidence labels are scoped to each question; a lexical mention alone is
not treated as evidence for an operational question.

| ID | Subject | Value | Exact support quote |
|---|---|---|---|
| `cl.dev.resume` | node alpha | Resume the pending operation from its last durable state. | The operation resumes after interruption. |
| `cl.dev.offset` | node beta | Recorded offset. | A checkpoint contains the durable offset needed to resume. |
| `cl.dev.wallpaper` | node gamma | Decorative theme. | Checkpoint is the codename of the purple wallpaper. |
| `cl.dev.crypto` | node delta | Protection setting. | Encryption protects stored records with a secret key. |
| `cl.dev.orchid` | node epsilon | Verification marker. | The orchid marker identifies the validated payload checksum. |
| `cl.dev.toolbar` | node alpha | The toolbar now uses compact icons. | The toolbar now uses compact icons. |
| `cl.dev.weather` | node zeta | A weather tile displays cloud cover. | A weather tile displays cloud cover. |

## Fixed requests

Every request forces `answer_question`, uses full detail and an explicit run.
The **literal query** is short to isolate the retrieval instrument; the task
description determines the independently written evidence labels. The native
engine receives only the literal query and configured anchor, not the labels or
description. This deliberately exposes the ambiguity of a short keyword query.

| Case | Literal query | Task represented / relevant evidence IDs | Anchor | Budget |
|---|---|---|---|---:|
| checkpoint_union | checkpoint | Restore an interrupted operation: resume + offset | alpha | 4096 |
| support_quote_only | orchid | Identify the validated payload marker: orchid | omega | 4096 |
| glossary_pl_en | szyfrowanie | Find stored-record protection: crypto | omega | 4096 |
| misleading_word | checkpoint | Restore an interrupted operation: resume + offset; wallpaper is irrelevant | omega | 4096 |
| no_answer | quasarflux | No record supplies evidence; gold set is empty | omega | 4096 |
| low_budget | checkpoint | Same evidence question as checkpoint_union | alpha | 1 |

Record IDs in the gold sets use the `cl.dev.` prefix above. The no-answer case
does not test an answer model's abstention: it measures emitted candidate and
selected false positives only.

## Fixed configurations

1. **graph**: relation_hops=1; no additional channels; shadow off.
2. **tfidf**: relation_hops=0; `tfidf` channel; shadow off.
3. **union**: relation_hops=1; `tfidf` channel; shadow off.
4. **graph_shadow**: same selection as graph; lexical shadow on. Shadow results
   are diagnostic and never inserted into selected context by this configuration.

The channel limit is 100, comfortably above the seven-claim corpus; min_score=0.
The native `tfidf` implementation uses `resolve::VectorSpace`, normalization and
the existing PL/EN glossary. It is a sparse lexical vector method, not a neural
semantic embedder. Existing project/stable retrieval remains enabled, but this
fixture has no such records. These settings therefore isolate claim discovery.

Mechanism expectations (record observations, do not tune them into quality gates):
graph can reach the unlabeled durable-resume claim; a text channel can retrieve
support-only evidence outside graph reach; their union may improve evidence
coverage while also introducing the misleading wallpaper record. Glossary
normalization may recover EN evidence from the PL query. Low budget can reduce
selected coverage even when retrieval discovered the evidence. Shadow may expose
lexical omissions but cannot discover evidence with no matching surface term.

## Metrics and denominators

Report one machine-readable row per case/configuration, and aggregate by
configuration only after retaining the rows. Deduplicate claims by ID.

- **Candidate evidence recall:** relevant prebudget claim candidates / gold
  evidence count. Prebudget selection candidates are the union of selected and
  budget-dropped claims in this dependency-free fixture. State this operational
  definition explicitly; it does not measure every internal scanned document.
- **Selected evidence recall:** relevant selected claims / gold evidence count.
- **Selected precision:** relevant selected claims / selected claim count.
  A zero denominator is `null`, never a fabricated perfect precision.
- **Ranking:** candidate and selected first-relevant reciprocal rank and
  hits@1/hits@3. Candidate ranking uses the engine's final candidate score
  descending, then claim ID; selected ranking uses emitted claim order. These
  measure evidence ordering, not the correctness of generated answers.
- For zero-gold queries, recall/RR/hits are `null`; report the number of
  irrelevant candidates and selected claims. Aggregate MRR/hits only across the
  five nonempty-gold cases, retaining their denominator. Aggregate micro recall
  and precision retain evidence/selected counts and include false positives from
  no-answer cases in the precision denominator.
- Shadow diagnostics retain channel status, candidates and omitted references
  as emitted by the native engine; they are not counted as selected evidence.
- Preserve actual selected IDs, candidate scores, token use, budget drops,
  channel diagnostics and pack hash so a percentage can be inspected.
- **Answer correctness: not measured.** There is no answer generation, paid call,
  external provider, human answer grading, or claim of semantic generalization.

The executable test asserts fixture integrity and accounting/instrument
invariants (budget, deterministic selection, shadow noninterference and metric
denominators). It does not assert retrieval-quality thresholds. The first output
must be retained even if it identifies a defect; corrections get a new receipt
and distinguish fixture/instrument repair from retrieval tuning.

## Execution status

Pending. Build and execution are owned by the W2 native-validation agent. The
fixture source is `loom/tests/test_context_retrieval_dev.cpp`; results will be
recorded separately after the first native run.
