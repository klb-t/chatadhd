# Annotation-level interpretation for a frozen scoring adapter

No model/parser/vector response was inspected to write this note. It clarifies
the existing full-turn evidence annotation level without changing source text,
gold labels, splits or the preregistered primary semantics.

Gold evidence quotes cover the complete turn so they preserve sufficient
context for attribution, negation and correction. A method can correctly cite
a smaller exact clause span within the annotated supporting turn. For the
primary grounded-edge measure, exact evidence binding means:

1. Match relation, directed endpoints, relation polarity, attributed speaker
   and original known_at; preserve the source assertion's temporal identity.
2. Bind the prediction to an annotated supporting turn with matching source ID.
3. Validate its reported quote, Unicode-codepoint span and UTF-8 byte span
   exactly against that turn's unchanged bytes.
4. Check the claimed clause evidence contains the relevant relation assertion
   or denial; turn-ID matching alone does not excuse citing an unrelated clause
   from a turn with multiple relations.

Do not call a properly grounded smaller clause false solely because the oracle
stores the entire turn. A stricter full-turn-equality mechanism metric can be
reported separately. Freeze any automatic/minimal-clause rubric and exception
policy before validation, and record unresolved grounding cases instead of
silently counting them as correct.

For temporal edge judgments, physically supply only turns with known_at at or
before as_of. Sending the complete conversation and asking the model to ignore
later turns is an optional future-leakage experiment with its own recipe.

The same proposition IDs are experimental handles for assisted extraction and
supplied-edge judgment. They are absent from the source-only extraction track.
Free node matching needs its own frozen alignment policy and must not borrow
gold aliases or report assisted-graph quality as unconstrained extraction.

Alternative formal paths have separate denominators: a correctly supported
ternary answer can cite one valid path; alternative-path recall still measures
which of the independently annotated minimal paths survived. A sourced direct
edge and an inferred two-hop consequence remain distinct even when both are
called supported in their respective task scopes.
