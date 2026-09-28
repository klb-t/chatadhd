# Repository source-structure diagnostic — 2026-09-28

Status: **source-only integration diagnostic, not semantic validation**.

The bounded grammar recognized **0 envelopes and 0 logical candidates** in all
three architecture documents, both before and after topic/sentence segmentation.
The pipeline exposes local topic/context candidates and preserves source spans,
but these measurements do not establish that it understands the documents.
Structured-gold proof and graph results must remain separate from this result.

## Reproduce

Run from the repository root:

```sh
python loom/tools/structure/repo_experiment.py \
  --pair-budget 16 --mode both \
  --output /tmp/loom-repository-structure-2026-09-28.json
```

The runner reads only the three named architecture sources, the self-profile
configuration and listed implementation dependencies. It does not read benchmark
fixtures, a database, owner exports or the temporal answer-key branch. No grammar,
semantic lexicon or threshold was changed for these sources. Full source copies
are not emitted; the compact report contains counts, hashes and source locators.

Two successive runs produced byte-identical **21,848-byte** JSON summaries, SHA-256
`cbc1ad54430c16dacc23142a6e0a25fa8b613ecfd4fd58d17a42ae951260c491`.
The runner hashes inputs/dependencies before and after measurement and refuses a
report if any changed during the run. A later source/code version should produce
its own report and hashes, rather than overwrite the interpretation of this run.

Measured components: `source-structure-pipeline/2`,
`topic-context-experiment/1`, `bounded-text-structure/2`,
`structure-experiment/2`, `scoped-thought-projection/1`.

## Source and segmentation accounting

The source bytes were decoded and re-encoded with a byte-identity check. There
was no Markdown stripping, line reflow, paraphrasing or invented sentence text.
Each document was analyzed independently in two layouts: one whole-document
turn, and exact slices ending at blank-line paragraph boundaries. Slices concatenate
to the original document, including whitespace. File locators, source hashes,
character/UTF-8 byte offsets and layout metadata are retained on the input turns.
These turns are document slices, **not historical chat turns**.

| Source | Raw nonempty physical units | Raw eligible characters | Pipeline observations | Pipeline eligible characters | Candidate topic segments | Recognized envelopes / logical candidates |
|---|---:|---:|---:|---:|---:|---:|
| Owner requirements | 198 | 19,128 | 272 | 19,054 | 179 | 0 / 0 |
| GPT note | 321 | 12,028 | 354 | 11,995 | 77 | 0 / 0 |
| Conceptual model | 358 | 27,538 | 530 | 27,366 | 257 | 0 / 0 |
| **Total** | **877** | **58,694** | **1,156** | **58,415** | **513** | **0 / 0** |

The original sources contain **61,577 bytes / 60,221 Unicode characters**.
Eligible-character denominators exclude the leading/trailing whitespace of each
extraction unit. The smaller segmented denominator reflects additional trimmed
unit boundaries; original bytes remain untouched. All 877 raw units and all 1,156
segmented units were reported unknown. Grammar character coverage was 0 in both
layouts. This is not a semantic-recall estimate because no gold meanings or
relevance labels were supplied.

The paragraph layout had 43, 178 and 78 turns, respectively. It produced the same
observation, candidate-boundary, grammar and focus counts as whole-document mode.
The current segmenter already splits at newlines, and it does not reset its
state merely because another source turn begins. IDs and input hashes differ
between layouts; identical aggregate counts do not imply general layout invariance.

## Topic and graph context accounting

The runner converts `loom/data/profiles/self.json` explicitly:
`t → text`, `requires_context.any/min → requires_any/min_context`, and
`negative_context → excludes`. It adds no ungated project-name aliases. All
aliases and context cues use the topic experiment's literal Unicode boundaries
and case-insensitive matching. Raw stem-like strings remain literal: for example,
a cue such as `reprezentacj` does not automatically match a longer inflected word.
This is different from native match-key/stem behavior. The topic experiment also
vetoes any local negative cue, rather than reproducing native negative-dominance
scoring. These counts cannot be compared directly with native catalog precision
or recall.

The context graph is a faithful **configuration projection**: 11 declared project
nodes and 8 explicit `merged_into` edges. It contains 57 configured aliases,
6 marked ambiguous. Node/edge provenance identifies the self-profile file hash
and exact JSON pointer. Project role descriptions remain metadata; they are not
reinterpreted as universal roles. `merged_into` remains lineage, not an alias
merge. No core Claim IDs, assessments, inferred facts or native graph truth are
invented. A neighbor returned by this graph is a context candidate only.

| Source | Literal alias occurrences | Accepted local occurrences | Rejected for insufficient local context | Observations with local focus | Context graph candidates |
|---|---:|---:|---:|---:|---:|
| Owner requirements | 10 | 6 | 4 | 5 | 20 |
| GPT note | 20 | 11 | 9 | 8 | 48 |
| Conceptual model | 16 | 5 | 11 | 5 | 14 |
| **Total** | **46** | **22** | **24** | **18** | **82** |

All 18 focused observations used `local_alias`; 1,138 were unanchored. No
corroborated-continuity or unresolved-continuation focus was produced on these
sources. This is a record of heuristic decisions, not correctness labels.
Context lookup omitted 0 nodes at its per-observation budget of 16.

## Comparison coverage and omissions

Per document and layout, the pair budget was 16. There were **0 eligible pairs,
0 evaluated pairs and 0 pair-budget omissions**, because no operation envelope
was recognized. All 513 proposed topic segments lacked recognized operations.
There were 0 scoped projections, 0 scope-limit refusals, 0 interpretation-context
proposals and 0 blocked logical candidates. No conclusion about pattern
similarity, cross-topic recurrence or inference quality can be drawn from an
empty comparison set.

Pipeline version 2 computes the eligible cross-segment pair count from group
sizes, and stops comparison after the visible budget. Its sample is source-order
exploration, not ranked retrieval or an estimate of population recurrence.
This diagnostic produced 0 Claims and applied 0 graph mutations. No semantic
accuracy or calibration value was calculated.

## Integration limits exposed

- **Segmentation alone does not solve grounding.** More local spans reduced some
  punctuation rejections but yielded no recognized meaning structures. The tiny
  full-envelope grammar is not yet a useful parser for these source documents.
- **Markdown and wrapped lines remain real inputs.** Bullets, tables, quote
  markers and mid-sentence line breaks are retained exactly. Adapting document
  parsing requires a reversible source map, not replacing the evidence with
  cleaned text and losing its original coordinates.
- **Topic and grammar boundary policies conflict.** There were 101 observed
  cuts after semicolons across the three sources. The extractor has operations
  whose complete envelope requires a semicolon (goal plus constraint), while
  the topic segmenter splits there. The code contract therefore permits the
  segmenter to destroy a recognizable envelope before extraction. This report
  does not label any particular corpus clause as a lost correct interpretation.
- **Topic boundaries remain hypotheses.** Most proposed boundaries arose from
  lexical novelty. Paragraph/Markdown structure and discourse scope are not
  semantically validated; 513 segments are not 513 established topics.
- **The profile adapter is intentionally literal.** Native normalization,
  alias resolution, project membership and philosophy relevance are separate
  tasks. The 22 accepted occurrences establish none of those accuracy measures.
- **No recurrence benchmark was exercised.** Structural scoring and scoped
  composition need grounded candidates. A good score on supplied logical gold
  cannot fill the zero-coverage gap observed here.

Next experiments should evaluate reversible document segmentation and grounded
interpretation on independently annotated source spans, while retaining
unrecognized material and the same distinct project/topic/structure/inference
axes. This checkpoint changes neither the production selector nor extraction
policy to make these diagnostic numbers look better.

## Source snapshots

| Repository file | SHA-256 |
|---|---|
| `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` | `e192c8a4b905972f14838140f98b0cffc87ca87a1f9f422ea072ed7c111c1d66` |
| `docs/architecture/NOTATKA_GPT_2026-09-26.md` | `726775d93537533403d7938fc996c7ff34d0a0908710666708a4df622319de1b` |
| `docs/architecture/LOOM_CONCEPTUAL_MODEL.md` | `b1a3bdd1efdd62451e63838a38d9234dbddd35ae315966fc0b5cd6f571d402b6` |
| `loom/data/profiles/self.json` | `338299e23bc6b8974601d72afb543d5bd19d5a7ca9fec4b6e1c4e3ea0cc3b155` |

## Implementation snapshot

All paths below are relative to `loom/tools/structure/`.

| File | SHA-256 |
|---|---|
| `repo_experiment.py` | `b63d64ea01cfb6b29014e2663ffaa7ebfef92ae476a567000d6408d78bf226bb` |
| `pipeline.py` | `0ebcf399cd1f11f456f3442394d7c32f217ab9c2745cf4da3662d201ff1290ed` |
| `topics.py` | `1175c60d5cdf80758603231e8e8746c517fcad9d69c26cb77d301786f994de28` |
| `topics_policy.json` | `2b042de18e46b0316b2eb673f2a3e851ea79b0cd530be63d748555788e07f5e6` |
| `extract.py` | `b88152833363f48808a8e4ab72050f6953343c0a8a3cdd91eb0883c43982fd59` |
| `structure_methods.py` | `89596297f279ad50b1599d2cb125fc7e140c065c91827d21f2c11ec4021e0fb3` |
| `operations.json` | `0bdd4dbe0a915577999c595c5f215be8cc0fa2c0c10405e9b101a2eb1a7c0ba1` |
| `scoped_projection.py` | `c9e31b89f681977b3889c147d285cafef60b412743a8f123d24a07e8a485d915` |
