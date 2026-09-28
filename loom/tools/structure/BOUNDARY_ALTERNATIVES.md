# Alternative envelopes across source-boundary cuts

`boundary_alternatives.py` adds a separate source-mapping experiment without
changing the frozen topic segmenter, parser, scoped projection or rule checker.
Its API is:

```python
alternatives = recover_envelopes(record, segmentation, primary_extractions)
```

Topic segmentation splits at semicolons, while the bounded parser recognizes a
complete `Goal: ...; constraint: ...` envelope. Parsing only the resulting topic
observations can therefore lose a structure that the unchanged parser recognizes
in the original source line. This pass parses each original turn's physical lines
independently and records missing envelopes as **alternatives**, not corrections
to the topic segmentation.

Each alternative retains exact original-turn character and UTF-8 byte offsets,
the source quote and text hash, every overlapping observation, and each
observation's own segment, local focus set, evidence references and boundary
status. Observation cuts and actual segment changes are reported separately.
There is no selected combined segment and no combined subject identity. Final
segment anchor summaries are never used. Every alternative requires scope review.

An envelope already present in primary extraction at the same turn span with the
same operation is suppressed. A conflicting primary slot/formula interpretation
at that same location raises an error rather than silently deduplicating it.
Different turns are never deduplicated just because their words match. Foreign
declared conversations, changed source hashes and inconsistent character/byte
coordinates also raise errors.

The result preserves the complete source record, full-turn parser results and
all unknown physical lines. `alternative_projection.structure` is a separate
operation/slot graph. Alternatives are excluded from primary coverage; they are
unchecked, lack Assessments, and are ineligible for inference, persistence or
automatic graph mutation. An overlap with two topics does not establish that
their entities are identical. An envelope with no observation overlap remains
explicitly unassigned.

The unchanged grammar and physical-line boundary still limit coverage. This
experiment does not reflow paragraphs, interpret arbitrary punctuation, prove
the alternative parse, or choose between conflicting interpretations.

The author-owned example `Goal: record music; constraint: preserve dynamics.`
has zero primary recognized envelopes after semicolon segmentation and one
recovered alternative overlapping both observations. A Polish example verifies
Unicode character/byte coordinates. An explicit reset policy on `constraint`
with separate Orion/Vega local aliases verifies that the recovered envelope
retains both focus sets without selecting one identity or borrowing later
anchors.

Verification:

```sh
python -m unittest discover -s loom/tools/structure -p test_boundary_alternatives.py -v
```

**9/9 tests pass**, including source preservation, exact deduplication,
cross-conversation rejection, conflicting primary payloads, untrusted logical
candidates and deterministic output. Only author-owned examples were used; no
independent fixtures or new evaluation outcomes were inspected for this change.
