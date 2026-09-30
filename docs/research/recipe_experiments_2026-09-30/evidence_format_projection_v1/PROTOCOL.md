# Exact-turn-ID evidence format projection of actual first responses

This secondary diagnostic development experiment was registered after the
source-only v1 run and its contract diagnostics were observed, before computing
this projection's output. It makes **zero new model calls**. The hypothesis is
that some primary false negatives are evidence-shape decoding failures, rather
than failures to propose the source relation.

## One variable and retained comparator

The only new operation is a versioned data policy: an evidence item that is an
exact nonempty string equal to one uniquely identified immutable raw turn ID
becomes `{turn_id: same_string}`. Apply the identical rule to source assertions
and status events. Existing evidence objects and every other model field remain
unchanged. Unknown, near, whitespace-altered or ambiguous IDs stay invalid;
no ID guessing, timestamp correction, relation/polarity/actor correction,
semantic aliasing, clause inference, event filtering or source rewriting occurs.

The original strict parser and `compile_free` bytes are pinned unchanged.
Duplicate JSON keys, truncated responses and all six originally unavailable
outputs stay unavailable. The alternate decoder does not parse them again or
choose one competing duplicate endpoint. The 24 original raw responses,
original ledgers, exact model objects and primary first results are immutable.
Preserve both original and derived model object hashes and pointer-level
conversion receipts; converted graphs remain unverified model-derived outputs.
Previously accepted typed assertions/events must remain byte-for-byte present.

This is a distinct syntax projection, not a weakened primary gate or an
unregistered reinterpretation of the first model run. Both decoders coexist.
No canonical graph writes or product adoption follow from these measurements.

## Frozen measurements and boundaries

Before reference labels load, freeze policy/code/tests/protocol plus original
source inputs, manifests, ledgers and 24 response hashes; persist all projected
first outputs and receipts. Reuse the unchanged strict surface-alignment scorer
and all original denominators: 24 conversations, 60 used reference atoms,
60 source assertions and 6 status events. Report node/assertion/event precision
and recall separately, availability, newly accepted record counts, mean raw
evidence conversions per planned case, losses and unchanged first cost.

Primary baseline is the original decoder, not the empty-graph baseline alone.
Strict alias counts are representation-dependent lower bounds; their FPs include
unmatched source expressions and do not establish hallucination. A separate
join to the already saved manual source-audit categories may describe semantic
adequacy after projections persist, with its author/nonblind limitations stated.
That audit cannot select conversions, labels, thresholds or records.

Binding source bytes proves a citation exists, not that a full-turn clause
entails an edge or that a status event has the same actor. Report any unsupported
relation/event accepted by this alternative explicitly rather than promoting
the recovered edge count into model reliability. No independent validation or
new model-quality population estimate is claimed.

Keep the alternate operation as a research decoding candidate if it isolates
format loss while preserving original evidence and exposing semantic defects.
Investigate any lost existing record or changed typed field; revert the new
projection if those occur. The original strict gate always remains unchanged.

```sh
python3 -B -m loom.tools.structure.free_evidence_format_projection_v1 freeze
python3 -B -m loom.tools.structure.free_evidence_format_projection_v1 evaluate
```

The live-v2 grammar intervention remains separate: it changes model input and
cannot be evaluated by renaming these original v1 response objects.
