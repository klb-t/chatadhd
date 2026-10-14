# Unnamed structures and multiple perspectives — 2026-10-14

## Current checkpoint — first increment verified; next slice open

Branch: `gpt/implicit-structures-2026-10-14`.
Fresh base: `6c55d46953569d8ca80b577fbe213c28cbf63353` (`origin/main`).
The earlier B branch and its P4b.1 checkpoint `c269b930` remain intact.
Integration into main belongs to Claude. This branch does not include that
older context-selection WIP.

The first product increment preserves every matched project perspective in
analogies/transfers; score no longer silently chooses one. Pair identities and
morphism IDs prevent evidence from different interpretations collapsing into
one claim. Similarity uses only the observed/derived/user evidence it records;
transfer records the source slot, both sides' shared roles, and condition
claims. Transferred claims cannot become transfer premises.

Direction-independent morphism expectations now use `$target_slot`, accepted
only in the morphism expected-property contract. Existing explicit slot refs
retain their meaning. The builtin descriptors were migrated via the pack
generator. Generalizer version 2 / pipeline version 8 and the version in the
run fingerprint keep historical runs intact instead of appending changed
interpretations into them. No database DDL or source-byte migration.

Current scope: **33/33 CTest entries, 277 native cases / 7697 assertions,
execution guard PASS**, 0 unexecuted entries, source unchanged during the run.
This includes seven perspective/migration cases, existing generalization and
evaluation, extraction, resolution, store/model, context and semantic callers.
Evidence: `perspectives-r2/` under the local directory below (receipt before
tests, exact source patch, manifest, complete JUnit, guard, before/after source
identity). Native mechanics only; no new quality measurement or full-matrix
claim yet.

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

The five initial regression cases all failed on the base product, while its
existing generalize suite passed (`before.*`). The first fix attempt was
rejected by the pack validator because the new reference was not yet declared;
that failed run remains in `perspectives.*`. The exact reference is now
accepted only where implemented; the successful rerun is `perspectives-r2/`.
An ordinary Debug build completed with the existing GCC 13 toolchain,
vendored SQLite, shared library and server. It uses `-g1`, assertions and
`-Werror`; no sanitizer weakening. Four build jobs match the four available
CPU cores; no competing heavy build. Local logs/checkpoints:
`/workspace/.onboarding/implicit-structures-20261014/`.

No paid model calls, paid CI, private corpora, main writes or foreign-branch
writes. Public synthetic inputs test mechanics, not LLM discovery quality.
No full-matrix PASS is claimed at this checkpoint.

## Next exact step

Implement the pack-defined connected relation join, store its bindings and
source evidence with the existing instance, and exercise a conversation whose
execution structure is never named “harness”. Negative cases must distinguish
a word list, disconnected/reversed edges, incompatible source versions, and
incomplete recognition. Arbitrary invention of new schemas remains a separate
open problem; a known-pattern matcher is not evidence that it is solved.
