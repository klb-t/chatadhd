# Bounded source-text extraction experiment

`extract.py` is an offline standard-library research adapter. It recognizes a
small, explicitly enumerated set of complete English and Polish text forms and
projects their slots to the graph-comparison experiment. It does not establish
source truth, discover arbitrary thought operations, or create production Loom
Claims. A recognized outer form is not complete semantic interpretation of its
opaque clauses.

## Input and source grounding

`extract_record(record)` requires an explicit `text` string. Records can carry
`id`/`record_id`, `source_id`/`source`, `locator`, `unit`, `turn_id`, `topic_id`,
`segment_id`, `source_span`, an upstream assessment, and unknown metadata. The
entire input is retained under `input`; candidate IDs are deterministic.
`locator.source` supplies source identity when an explicit source ID is absent.
Source identity is marked supplied/unverified or missing. This adapter does not
retrieve a source container or verify a supplied source hash.

Every recognized envelope, extracted slot and unsupported nonempty physical line
has exact half-open character and UTF-8 byte offsets in `input.text`, with its
verbatim quote. A supplied outer locator/span is preserved, not silently treated
as a compatible coordinate system. The text hash covers the supplied Unicode
text encoded as UTF-8; it is not a claim about the original source encoding.

`extract_claim(claim, observations)` follows
`assessment.basis.support[].observation`. It only extracts a support quote that
occurs exactly once in the referenced observation. Missing observations, missing
quotes, mismatches and repeated ambiguous quotes explicitly abstain. The whole
observation text, the quote's observation-relative span, the claim assessment and
the support record remain available. An arbitrary claim `value` never substitutes
for an observation quote.

Records, turns and topic segments are never merged. Physical lines are considered
independently. This conservative boundary is a major recall limitation for
line-wrapped documents. Upstream topic segmentation can supply shorter records;
the adapter does not infer topic boundaries or join continuations itself.

## Supported forms and logical limits

| Source form | Structural representation | Logical candidate |
|---|---|---|
| `If P, then Q` / `Jeśli P, to Q` | Conditional with opaque condition/consequence | Implication only if both clauses match the restricted copular grammar |
| Above plus `, else R` / `, w przeciwnym razie R` | Branch with three slots | None |
| `P, unless Q` / `P, chyba że Q` | Default and exception slots | None |
| `All X are Y` / `Wszystkie X są Y` | Universal inclusion statement | Universal implication between opaque class predicates, for restricted class labels |
| `X is a kind of Y` / `X jest rodzajem Y` | Type inclusion statement | None |
| `X is more D than Y` / `X jest bardziej D niż Y` | Dimension and ordered alternatives | None |
| `Goal: G; constraint: C` / `Cel: G; ograniczenie: C` | Goal and constraint slots | None |
| `We want to G, subject to C` / `Celem jest G; ograniczeniem jest C` | Goal and constraint slots | None |
| `NAME is PROPERTY` / `NAME jest PROPERTY` | Copular assertion | Unary atom for restricted one-word subject/property |

The grammar also accepts `Jeżeli`. It does not translate, stem, resolve aliases
or pronouns, infer singular/plural equivalence, guess negation scope, or interpret
modal/quantified clauses as simple assertions. Ambiguous nested attachments and
multiple sentences on one physical line abstain. Unsupported material is retained
under `unknown`, including reasons and exact spans.

Restricted logical symbols use casefolding and whitespace normalization only:
`property:<text>` for copular predicates, `class:<text>` for class predicates,
and source names for constants. A universal statement is not evidence that its
speaker performed empirical induction; type inclusion is not evidence of a
specialization procedure. A goal with a constraint does not state the means,
causal consequences or recommended action.

## Projection and eligibility

`compare_extractions(left, right, projection="operations")` compares the outer
operation/slot graphs. It can find the same envelope despite different opaque
clause meanings, so its similarity must not be presented as logical equivalence.

`projection="logical_candidates"` compares restricted formulas instead. Each
candidate formula is projected independently: repeated identity within a formula
is retained, while inter-line or cross-topic coreference is not assumed.

`candidate_logic(..., accept_unchecked=True)` is an explicit projection-only
conversion. It retains `extraction_status="unchecked"`, includes no Assessment,
and carries no calibrated confidence. The rule checker therefore rejects these
entries as ineligible premises. The flag does not verify extraction or authorize
promotion to observed evidence. All results have `persistable_claim=false` at the
extraction boundary. Parser-created confidence is `null`; upstream confidence is
retained verbatim in the input, never reused as confidence in the interpretation.

## Measured developer checks, 2026-09-28

The initial parser freeze was
`b07d8f01656b1ff52247e0bf6eabe86a671b75d103e8f49aa9657741cf3914f0`
(SHA-256 of `extract.py`, version `bounded-text-structure/1`). Its independently
authored developer unit examples do not read any evaluation fixtures or answer
keys. `python -m unittest discover -s loom/tools/structure -p test_extract.py -v`
passed **15/15 tests**. These verify source spans, preservation, abstention,
eligibility blocking, scope isolation and actual source-text-to-graph comparison;
they are not a semantic accuracy estimate.

After the independent evaluator copied that frozen implementation and before any
validation outcomes were shared, version 2 corrected only descriptive metadata:
universal/type inclusion now use family `category_inclusion`, and goal/constraint
uses family `goal_constraint`. Version 1's broad family labels
`generalization`/`specialization`/`means_end` could suggest operations that the
source did not state. Recognition grammar, formulas, graph labels and comparison
behavior did not change. Version-derived IDs naturally changed. The first-run
independent evaluation remains identified by its version-1 snapshot and hash.

A small developer pipeline experiment starts with these unannotated text inputs:

- A: `If pump is active, then pump is ready.`
- B: `If singer is calm, then singer is prepared.`
- C: `If pump is active, then valve is ready.`

| Pair | Lexical cosine | Logical WL cosine | Exact structural alignment |
|---|---:|---:|---|
| A–B, consistent same-entity pattern in different domains | 0.500000 | 1.000000 | Isomorphic |
| A–C, shared words but changed entity identity | 0.912871 | 0.792118 | Different |

This establishes that the bounded path can use entity identity and argument
structure beyond lexical overlap. It does not establish broad parser coverage or
the truth of either statement. All formulas remain unchecked.

An untuned diagnostic passed each complete repository document below directly
to `extract_record`; no Markdown cleanup or sentence reflow was performed:

| Source | Nonempty physical lines | Eligible characters | Recognized envelopes | Logical candidates |
|---|---:|---:|---:|---:|
| `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` | 198 | 19,128 | 0 | 0 |
| `docs/architecture/NOTATKA_GPT_2026-09-26.md` | 321 | 12,028 | 0 | 0 |
| `docs/architecture/LOOM_CONCEPTUAL_MODEL.md` | 358 | 27,538 | 0 | 0 |
| Total | 877 | 58,694 | 0 | 0 |

Eligible characters exclude outside whitespace on each line. All nonempty lines
remained explicit unknowns. **Zero coverage is a failure of this narrow grammar
on these inputs**, not evidence that the documents lack structure. This adapter
is a bounded demonstrator, not a general archive extractor. Independent parser
coverage and gold-structure scoring must be reported separately from comparison
on supplied logical annotations. No evaluator examples may be used to tune a
parser while continuing to call the same evaluation held out.

## CLI

```sh
python loom/tools/structure/extract.py input-records.json \
  --output extraction-report.json --compare record-a record-b
```

The input is a JSON array of independent records. The optional comparison reports
both operation-envelope and logical-candidate projections. Writes are atomic.
No network, model provider, database mutation or core ABI change is involved.
