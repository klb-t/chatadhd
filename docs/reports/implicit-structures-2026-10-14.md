# Unnamed structures and multiple perspectives — 2026-10-14

## Current checkpoint (work in progress)

Branch: `gpt/implicit-structures-2026-10-14`.
Fresh base: `6c55d46953569d8ca80b577fbe213c28cbf63353` (`origin/main`).
The earlier B branch and its P4b.1 checkpoint `c269b930` remain intact.
Integration into main belongs to Claude. This branch does not include that
older context-selection WIP.

Owner's example: brainstorm, specification, code, tests, and an agent harness
that was not named in the original list. Examples are calibration cases, not
a closed or mandatory taxonomy (R8/R10/R35/R53). Recognizing source-described
structure, proposing an unmentioned component, and discovering a new reusable
pattern are different evidence claims.

## Confirmed code gaps and intended increments

1. `ParadigmMatcher::analogies` and `transfer` keep only one project-kind
   instance per subject. A higher-scoring unrelated reading can erase a valid
   alternative analogy/transfer. The score is not an owner decision.
2. Analogy scoring uses inferred roles while its recorded premises omit them.
   Transfer records the transferred source slot but not the two subjects'
   role evidence. Reverse transfers bind the forward destination's property.
3. Existing matching fills subject-to-slot stars. `DomainKind.relations` alone
   does not execute a join between fillers. A list of artifact words therefore
   cannot establish the relationships of an execution structure.

Implementation decisions (reversible, not new owner requirements): preserve
qualified instance pairs and evidence; use the existing pack, extraction,
generalization and KnowledgeStore for any relational matching extension.
Pattern labels, required relations and interpretation alternatives belong in
data. A match is not proof of an unmentioned executor or permission to run it.

## Validation and publication

The initial five regression cases are test-first; not yet run or passed.
An ordinary Debug build is in progress with the existing GCC 13 toolchain,
vendored SQLite, shared library and server. It uses `-g1`, assertions and
`-Werror`; no sanitizer weakening. Four build jobs match the four available
CPU cores; no competing heavy build. Local logs/checkpoints:
`/workspace/.onboarding/implicit-structures-20261014/`.

No paid model calls, paid CI, private corpora, main writes or foreign-branch
writes. Public synthetic inputs test mechanics, not LLM discovery quality.
No full-matrix PASS is claimed at this checkpoint.

## Next exact step

Complete the base build with the new regression tests, record its executable
receipt and failing cases, then fix multiple-perspective transfer/analogy and
directional expectations. After scoped gates, publish a verified increment
before implementing the relation-join slice.
