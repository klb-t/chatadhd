# Unnamed relational structures and qualified analogies — 2026-10-14

## Current verification checkpoint — 2026-10-15 21:30 UTC

Full dev and Clang on `efd969c7` both passed: **204 selected, 203 executed,
1355 native cases / 47931 assertions / 2077 Python cases**, zero Python skips,
one catalog-scale opt-in unexecuted, guard PASS, unchanged source. These are
this branch plus the two accepted upstream fixes, not a merged-main validation.

Web build and all **26 package test scripts** now have successful executions:
21 on the original system-Chromium pass, four after installing the package-pinned
Playwright headless shell, and `test:interface-2` after narrow upstream test fix
`85a406dd` → `588ac547`. The latter waits for model options to load after reload;
both exact model-value assertions remain. No product code or timeout changed.
The original five web failures and the replay's model-picker race remain in logs.
The native interface runner completed 20/20 groups; native conversation view
completed 9/9, including source loss, reopening and absence of persistent payload.
Fresh evidence directories preserve earlier receipts rather than overwrite them.

The subsequent ASan process was interrupted with the previous session after
37/203 entries, with no final JUnit/guard. It is **unverified, not PASS**.
A new full ASan run is next, on the same native sources and binaries; its CLI
300-second timeout remains unchanged. No concurrent heavy build is scheduled.
The branch remains a review candidate for configured-pattern recognition and
qualified analogies; same-project code/ideas/spec/tests views remain open.

## Historical upstream intake checkpoint — 2026-10-15 UTC

Narrow upstream fixes accepted with authorship and `cherry-pick -x` provenance:
`e368d7d1` → `b2051da1` (graph-entry temporary lifetime),
`21cb0566` → `f74f0e1a` (storage fixture binary-array length). No other main
commits or branch state were imported. Scoped ASan on the composed tree passed
**8/8 entries, 79 native cases / 2027 assertions, guard PASS**,
including the previously crashing graph/storage consumers, server smoke and
both new generalization suites. Full subsequent matrix remains pending.

The completed pre-intake ASan (`full-asan-v4-r2/`, source `0e5fe095`) is **FAIL**:
203 selected, seven failures. Four native consumers and server smoke exposed
the graph-entry defect; storage exposed the fixture overflow; `cli.smoke`
exceeded its unchanged 300-second CTest timeout. The full log/JUnit and failed
guard are retained. No failure is treated as a skip or a PASS. Main's separate
CLI timeout increase (`030f404d`) has not been accepted; the existing bound stays.

Fresh main observed: `92818518` (320 commits after this branch's base).
A merge-tree preview before intake found textual conflicts only in STATE/INDEX;
the knowledge-engine fingerprint merged with main's path confinement. This is
not execution evidence for the merged main tree. C API fix `0e5fe095` is identical
to main `d47f04f3` and must not be counted as a new main change.

## Canonical comparison — verified scope, 2026-10-15 UTC

Code `b5e67f45` is now verified by `comparison-scope/`: **35/35 entries,
293 native cases / 8746 assertions, guard PASS**, no unexecuted entries and
unchanged source. Ten implicit-structure cases include the four previously red
comparison assertions. The old WIP label in the commit history records its
state when saved; this later evidence completes the scoped increment.

The qualifier filter reuses the established `json::canonical` contract:
object key order is irrelevant; arrays retain order; integer/float values,
large integers and signed floating zero keep their canonical representations.
No unknown fields are dropped and no strings are converted to numbers.
Generalizer 4 / pipeline 10 invalidate earlier computation identities.

Actual CLI/store migration also passed: unchanged source, new run,
byte-equivalent historical result, six cache hits on the repeat, `llm: off`.
[Migration result](implicit-structures-2026-10-14-evidence/migration-v4-result.json),
[pre-run receipt](implicit-structures-2026-10-14-evidence/migration-v4-receipt.json),
[current recognition](implicit-structures-2026-10-14-evidence/example-v4-recognition.json).

Full v4 dev on `f3ce2c18` passed: **204 selected, 203 executed, 1355 native
cases / 47930 assertions / 2077 Python cases, zero Python skips, guard PASS**.
The one existing opt-in catalog-scale entry remains unexecuted. The interrupted
ASan run completed only 41 entries and has no final JUnit/guard; it is not a PASS.
It exposed a C API test leaking its caller-owned settings string and an independent
import compatibility failure when Python/native generated titles straddled a
minute (00:48 / 00:49). Neither failure is suppressed.

The C API check now uses the existing ownership-taking helper and retains the
secret-nondisclosure assertion. Its ASan rerun passed **16 cases / 2634 assertions**,
leak detection enabled, guard PASS (`asan-leak-fix/`). Full ASan, rebuilt dev,
Clang and web are next on that test-only follow-up. The v3 results below are historical.
ASan v3 was interrupted at step 229/408 before tests; its retained log is
`build-asan-interrupted-v3.log`. No sanitizer PASS is claimed for that build.

## Historical v3 checkpoint — full dev verified, 2026-10-15 UTC

Published product: `79187377` (project-kind alternatives / transfer evidence),
`298b429b` (relational recognition and durable receipts), `f682fc89` (exact pack
model expectations). Branch: `gpt/implicit-structures-2026-10-14`; main unchanged.

Historical v3 full dev: **204/204 CTest entries accepted, 203 executed,
1354 native cases / 47921 assertions, 2077 Python cases,
guard PASS**. One catalog-scale entry is explicitly opt-in/unexecuted; zero
Python skips. This is the corrected run `full-dev-r2/`, with code/configuration/
binary receipt written before execution and unchanged source during it. The
previous `full-dev/` failure remains history. [Receipt](implicit-structures-2026-10-14-evidence/dev-receipt.json),
[guard](implicit-structures-2026-10-14-evidence/dev-guard.json).

The preceding scope passed 34/34, 286 native cases / 7834 assertions. Five
generator checks passed: four `--check` commands and KB regeneration/hash
identity. Full ASan, Clang and web gates are pending at this checkpoint. Next:
serialized ASan build, then its full runner; no competing heavy builds.

## Historical first increment — verified scope

Branch: `gpt/implicit-structures-2026-10-14`.
Fresh base: `6c55d46953569d8ca80b577fbe213c28cbf63353` (`origin/main`).
The earlier B branch and its P4b.1 checkpoint `c269b930` remain intact.
Integration into main belongs to Claude. This branch does not include that
older context-selection WIP.

The first product increment preserves every matched project-kind interpretation in
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

First-increment scope (historical): **33/33 CTest entries, 277 native cases / 7697 assertions,
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

## Initial findings — resolved by the increments below

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

## Relational recognition (reversible implementation decision)

The second increment adds `relation_pattern` to the existing pack anchor
operations. It is a connected directed join over existing entity IDs and claim
IDs, including cycles and incoming edges. It neither introduces another graph
engine nor invents missing edges. The new `feedback_execution` facet is an
example: project membership, tool use, verification and feedback can establish
a candidate pattern even if the conversation never says “harness”. Typed PL/EN
phrase templates supply these relations through the existing extractor.
Co-occurring artifact names alone do not establish the pattern.

Data specifies node kinds, directed edges, distinct bindings, qualifier
compatibility, evidence/status admission, exact extra-qualifier filters,
interpretation labels and search budgets. `max_steps` counts visited candidate
claims; `max_matches` counts complete proof bindings (not unique project names).
Zero means unlimited. Exceeding either reports `Unavailable` and an incomplete
recognition error; it never returns a successful truncated or empty answer.
Iteration order follows stable claim IDs. `claim_qualifiers` explicitly chooses
accepted typed values and whether missing legacy qualifiers are admitted. The
builtin admits positive/asserted or legacy-unspecified claims; negative,
unknown, hypothetical and quoted qualifiers do not prove this positive pattern.
A deliberately different hypothesis profile can change admission, while the
result remains a candidate. This grants no executor or disclosure permission.

The additive `Instance.recognition` object stores method/generalizer version,
resolved pack hash, reasons, descriptor, candidate alternatives, variable
bindings, per-edge claim IDs and supporting observation IDs. Source spans and
source units remain in the existing KnowledgeStore. The regular knowledge query
and the existing Workbench “All model fields” inspector expose this receipt.
Legacy instances without the field retain their JSON shape; no DDL migration.
The initial Generalizer 3 / pipeline 9 invalidated the computation identity
without rewriting older runs; the canonical-comparison follow-up advances these
to 4 / 10, with the same history-preservation rule. Existing extracted claims and source bytes remain intact.

The example's “agent harness” and “supervised workbench” are alternative
interpretations. The matched relationships alone cannot establish autonomous
control, an initiator, implementation completeness or a best interpretation.
This is known-pattern recognition; arbitrary open-ended schema discovery and
its measured quality remain open. No live model service is invoked.

### Owner clarification: perspective is a view of the same project

On 2026-10-14 the owner clarified that a perspective is, for example, viewing
one project through its codebase, or through its ideas and brainstorm, or its
specification, tests or agent harness. These can expose different structures
and relations belonging to the same project; they are not competing labels.
An alternative interpretation of a detected structure is a separate concept.
The earlier “perspectives” regression filename and commit title refer to
project-kind interpretations (software/film/music), not completion of those
same-project views. That wording is historical, not a feature claim.

The established `loom.graph_perspective/1`, source adapters, R40 resolution and
workspace projections remain the integration boundary for those views. This
increment does not replace them or change their owned module. Recognition
receipts may become evidence consumed by those projections; a morphism maps
stated relations between structures and is not merely the act of switching a
view. Wiring code/ideas/specification/test views with traceable cross-view
relations is a next product increment, not something this matcher proves.

### Verification notes and limits

Real synthetic conversation fixtures exercise PL/EN extraction, generic joins,
multiple bindings, incoming edges, missing/reversed edges, incompatible source
branches/versions, evidence and qualifier admission, configurable budgets and
pack validation. The runtime case runs all six actual pipeline stages, pauses
at generalization, closes and reopens the store, resumes, restores recognition,
reuses the cache and checks zero controlled-transport requests. No fake pipeline
stage is installed. Changed source bytes select a new run; the prior receipt
remains byte-for-byte unchanged.

Full catalog import deliberately includes retained historical units. The source
change case does **not** claim that this is a latest-source-only view: an old
valid pattern may still be supported by a retained snapshot. The initial test
incorrectly expected its disappearance; that failure remains in `relations-r3/`.
Incoming-edge testing initially removed the parent project's own `has_component`
anchors; it now retains those and adds the actual inverse edges being tested.
Neither change weakens product checks or alters import semantics. Latest-only
source scoping and assigning source-version qualifiers to every extracted edge
are separate integration work; blank qualifiers do not prove source freshness.

A read-only review found the negative/hypothetical admission gap; five such
negative assertions failed before the filter (`admission-before/`). An additional
failure there was a malformed transferred test fixture (it used an unrelated
extra field); the corrected fixture uses the actual derivation morphism field.
Two initial scoped runner calls selected a wrong CTest name and executed zero
tests (`relations-before/`, `relations-r1/`); both failed the guard and count as
no validation. `relations-r2/` records an undeclared relation in a data-change
test; it now uses a declared, different relation. All failed logs are retained.

## Historical resume point after the relation increment

The scoped publication and corrected full dev run described above supersede
this checkpoint. The current status is at the top of this report.

## Full-matrix follow-up

The first complete dev run on `298b429b` selected 204 entries and failed two:
`unit.test_pack_model` still expected the prior exact three-facet catalog;
`research.structure` rejected the TMPDIR Git ancestry in the restricted
filesystem. Neither is a PASS. The pack-model expectation is updated to the
exact four facets, with explicit candidate/no-invented-slot checks. For the
rerun, local tests use `/var/tmp` through platform-reviewed execution so the
credential fixtures have a genuinely writable location outside Git. No marker,
negative case, timeout or secret protection is removed. Full evidence remains
in `full-dev/`; subsequent current gates receive new directories.

The follow-up scope (`pack-model/`) passed 2/2 CTest entries, 15 native cases / 1026 assertions, execution guard PASS. The opt-in catalog-scale entry in the full manifest remains explicitly unexecuted.

## Reproducible public product example — 2026-10-15 UTC

The ordinary CLI completed all six pipeline stages on a public synthetic
conversation, then the public `loom_kb_query` API reopened the same store and
returned one candidate with five proof edges. Evidence includes the
[exact input](implicit-structures-2026-10-14-evidence/example-source.json),
[configuration](implicit-structures-2026-10-14-evidence/example-config.json),
[recognition receipt](implicit-structures-2026-10-14-evidence/example-recognition.json)
and [pre-run binary/input hashes](implicit-structures-2026-10-14-evidence/example-receipt.json).
The input never names a harness: it describes components, a tool, verification
and feedback. No claim of autonomous control or discovery quality follows.

From the repository root, with a fresh data directory:

```sh
example_data="$(mktemp -d)"
loom/build/dev/cli/loom --data-dir "$example_data" --json --quiet knowledge run \
  --source docs/reports/implicit-structures-2026-10-14-evidence/example-source.json \
  --config docs/reports/implicit-structures-2026-10-14-evidence/example-config.json
```

Use the returned run ID with the existing knowledge query (`what: instances`,
`paradigm: software_app`) to inspect `recognition`. Absolute source paths may
change run/observation identities; the structural acceptance is one candidate,
five proof edges and retained source references, not identical host paths.
The configuration explicitly sets `llm: off`; this is a mechanism example,
not an LLM quality measurement.
