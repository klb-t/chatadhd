# Structure round 3 independent fixture protocol

Protocol frozen on 2026-09-29 before opening any thought-structure parser,
existing source-document benchmark input, parser result, or model response.
The fixture author read only the project work contract and the R22–R24
requirements. No paid or local model generated or judged these labels.

## Design

- 96 newly authored source sentences; no sentences copied from repository
  source documents or earlier benchmarks.
- Eight families: conditional, cause, contrast, exception, means_goal,
  generalization, conjunction_alternative, evidence_claim.
- Development: 64 cases, eight per family: four supported / four abstain,
  with two PL / two EN in each label group.
- Sealed validation: 32 cases, four per family: two supported / two abstain,
  with one PL / one EN in each label group.
- Every supported case has exactly one annotated explicit relation. The
  family expresses an observed source assertion, not proof that its operands
  are true or that the assertion is causally/logically valid.
- Every operand and cue has exact half-open Unicode codepoint and UTF-8 byte
  spans into the unchanged source sentence. Operand punctuation is excluded
  unless it is internal to the operand.
- Questions, quoted/code/metalinguistic examples, denied relations, idiomatic
  cue words, and unresolved scope are intentional abstentions under the
  present unembedded-assertion policy. Their presence in a source is still
  an observation; this benchmark does not deny that quoted arguments could
  be separately analysed with explicit speaker/scope attribution.
- Explicit negation inside an operand is supported and must be preserved.
  Negating the existence of the relation is an abstention.

## Evaluation fixed before results

1. Freeze these fixture files and their SHA-256 manifest before running any
   parser or model. Inspect development labels only during development.
2. Evaluate an unchanged baseline on development. Save first predictions.
3. Improve on development; keep the operation mapping and strict scoring
   adapter fixed before opening validation labels. Record the implementation
   commit/tree hash and adapter hash at that point.
4. Run validation once. A failure diagnoses the method; it is not permission
   to repair the frozen method against this validation set and call that a
   fresh validation. Any later reuse is explicitly development reuse.
5. Report extraction precision and recall separately with TP/FP/FN counts.
   A supported case with no result is FN; an abstain case with a result is FP.
   An output matching family but wrong operand text/span or direction is both
   FP and FN in strict relation scoring. Report family-only detection as a
   separate secondary metric, never as strict extraction quality.
6. Report per-family denominators and the number of abstentions. Store case
   IDs and mismatch categories for failures. The positive/negative balance is
   deliberately artificial; metrics do not estimate prevalence in real text.
7. Evaluate existing source documents separately after fixture development;
   do not conflate their parser coverage with labelled extraction recall.
8. Decisions are keep/revert/investigate against the unchanged baseline and
   existing regression gates. Tests of span integrity are mechanism tests,
   not measurements of parser or model quality.

## Access control

`dev.json` and this protocol may be sent to parser developers. Do not open,
print, send, or discuss `validation.json` labels until the implementation and
scoring adapter are frozen. Merely having the file locally is not a claim
that it remained blind: record every evaluation and any accidental access.
`manifest.json` discloses only file hashes/counts, not validation sentences.

This is an independently authored synthetic validation set, not a natural
corpus and not the repository's forbidden temporal holdout answer key.
