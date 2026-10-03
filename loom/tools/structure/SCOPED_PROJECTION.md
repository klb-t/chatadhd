# Scoped thought-composition experiment

`scoped_projection.py` version `scoped-thought-projection/1` compares collections
of unchecked source-derived formula candidates within one explicitly declared
conversation segment. It adds a binding hypothesis to the existing parser; it
does not change the recognition grammar or upgrade extraction to verified logic.

The API is:

```python
projected = project_scope(extractions, binding="literal_within_scope")
comparison = compare_scopes(projected, another_scope, budget=10000)
```

Each supplied extraction must preserve nonempty `input.conversation_id` and
`input.segment_id`. Source IDs cannot substitute for conversation IDs because a
source archive can contain several conversations. Different turns within one
segment are allowed. Mixed conversations or segments, conflicting duplicate scope
metadata, duplicate extraction/observation/candidate IDs, inconsistent provenance,
invalid local spans and omitted non-whitespace source content raise errors.
No source content is silently truncated. Segmentation itself may be tentative:
`scope_status`, `scope_evidence`, `focus_basis`, source-identity status and unknown
statements remain visible in the evidence record.

| Binding mode | Symbol treatment | Interpretation |
|---|---|---|
| `separate` | Each formula is projected separately; equal symbols in different formulas remain separate vertices | Control with no cross-statement identity assumption |
| `literal_within_scope` | Exactly equal parser predicate/constant symbols share vertices across formulas in the declared segment | Conditional hypothesis of lexical coreference, not verified identity |

The literal policy uses existing parser symbols, which already casefold and
normalize whitespace. It adds no synonym matching, stemming, translation,
entity lookup or pronoun resolution. Shared symbols and their candidate IDs and
formula paths are listed under `binding.cross_statement_symbols`. Each binding
is visibly marked `assumed_same_referent`; `identity_verified` remains false.
Quantifier variables retain separate local binders and are never connected merely
because their variable names match.

Three comparison views are available:

- `logical_structural` preserves consistent symbol reuse and argument structure
  while allowing a consistent renaming across the compared scopes.
- `logical_literal_semantic` additionally preserves literal predicate/constant
  labels. Despite its name, this is literal symbol agreement, not a determination
  that two words have the same meaning or that two mentions identify one entity.
- `atomic_operations` compares the recognized operation/slot envelopes. These
  graphs are rebuilt from retained candidates so a stale supplied graph cannot
  silently add or remove recognized source material. Opaque slots do not create
  cross-statement bindings.

Each projection retains all original extractions, every candidate's source
provenance, unknown statements and recognized envelopes that have no logical
candidate. Exact local quotes and UTF-8/character offsets are checked again.
Outer archive coordinates and raw source identity remain unverified. Coverage
describes recognized input, not semantic accuracy. Empty graphs yield an
`unrepresented` alignment; they are not evidence of agreement or difference.

All logical entries have `extraction_status="unchecked"`, no Assessment and
`confidence=null`. Even an upstream user/observed Assessment is not copied into
the projection input. Inference eligibility, automatic mutation and Claim
persistence remain false. Structural resemblance does not establish validity,
truth, entailment, applicability or a recommendation.

The graph represents an unordered collection of statements. Their input order is
preserved in the source records but is **not encoded as temporal or argument
sequence**. The projection also does not determine which statement is a premise,
conclusion, quotation, rejection or correction unless that distinction is already
represented inside its unchecked formula. A matching collection is therefore
not a verified argument.

## Author-owned experiment

Each line below was independently passed through `extract_record`; no logical
annotation was supplied to the parser.

| Scope | Statement 1 | Statement 2 |
|---|---|---|
| A | `If pump is active, then pump is ready.` | `Pump is active.` |
| B | `If singer is calm, then singer is prepared.` | `Singer is calm.` |
| C | `If pump is active, then pump is ready.` | `Valve is active.` |

| Pair | Lexical cosine | Separate-formula structural WL | Scoped-literal structural WL | Scoped exact alignment |
|---|---:|---:|---:|---|
| A–B: same reuse pattern in different domains | 0.440000 | 1.000000 | 1.000000 | Isomorphic |
| A–C: similar words, changed second-statement referent | 0.960159 | 1.000000 | 0.837121 | Different |

Separate-formula exact alignment calls both pairs isomorphic because the linkage
between statements has been removed. Scoped literal binding distinguishes the
second pair under its declared coreference hypothesis. Literal-semantic exact
alignment calls both pairs different because their literal labels differ.
Atomic-envelope comparison remains isomorphic for both pairs. The views answer
different questions; none alone verifies the underlying interpretation.

This is an author-owned mechanism demonstration, not a held-out accuracy result.
No independent fixture or answer key was read. Source coverage limitations of the
unchanged bounded parser still apply.

Disclosure chronology: the scoped implementation and its first 13 author-owned
tests existed before an unsolicited message disclosed aggregate results from the
earlier version-1 parser evaluation. No validation examples were disclosed. Those
aggregate results were not used to change this method. Subsequent changes fixed
four source-consistency defects found by a separate read-only code review, added
two regression tests, and completed documentation. The prior aggregate outcome
exposure means a new evaluation of this method must use a fresh withheld
extension rather than describe the earlier dataset as unseen.

## Bounds and verification

`project_scope` defaults to at most 64 logical formulas and 256 nodes in each
projected graph. Explicit graph limits cannot exceed 512 nodes, keeping this
prototype below the exact aligner's recursive-search depth hazard. Oversized
inputs raise an explicit error rather than dropping statements. Unknown-only
source records are retained even when they add no graph vertices.

`compare_scopes` requires matching binding policies and applies a visible
alignment search budget independently to each of its three projections. Search
exhaustion means unknown. Neither search budgets nor cosine similarities are
confidence estimates. These bounds make a small experiment reviewable; they are
not a claim of production-scale performance.

Verification command:

```sh
python -m unittest discover -s loom/tools/structure -p test_scoped_projection.py -v
```

**15/15 tests pass**, covering the composition discriminator, semantic/structural
separation, scoped assumptions, unknown-source retention, quantifier locality,
source integrity, inference blocking, duplicate handling, bounded search and
explicit lack of sequence semantics. The implementation and developer examples
are offline and make no database, provider or core ABI changes.
