# Applying the Gemini research to Loom

The two supplied reports identify useful components for the existing graph:
document semantics, discourse relations, formal consequence checking, cheap
classification and reusable context. They do not establish a universal extractor
or an infallible classifier. This increment implements two bounded offline
mechanisms and keeps their measurements separate from live-model quality.

Primary-source audits are in `GEMINI_LOGIC_AUDIT_2026-09-28.md` and
`GEMINI_JEV_AUDIT_2026-09-28.md`. Text snapshots and hashes of the attached DOCX
files are in `inputs/gemini-report-provenance.json`; the text extraction does not
preserve Word layout or all non-text OOXML.

## What was used

| Research direction | Concrete use in this increment | Boundary |
| --- | --- | --- |
| Formal verification, including FoVer's consistency gate | `logic_check.py` checks selected premises before testing a conclusion; it returns counterexamples and witness assignments | A supplied propositional interpretation is checked, not the fidelity of extraction or truth of source claims |
| Jev's narrow independent decisions | `context_scores.py` replays per-candidate relevance scores bound to the source packet and question/rubric | No Jev calls, model quality, price saving or automatic project assignment measured |
| Sensitivity to naming/order | An explicit candidate mapping compares saved decisions and score drift | The mapping is caller-declared; agreement is not correctness |
| IE-as-Cache | Reuse the existing selected-context packet and original-source handles instead of creating another authoritative store | No raw-versus-cache model benchmark has run; source fallback remains necessary |
| UMR and dialogue discourse parsing | Retain coreference, temporality, modality and competing interpretations as distinct future projections/relations | Neither UMR parity nor Polish conversation parsing is implemented by these two modules |

## Formal checking without accidental proofs

The input is an already validated occurrence-graph bundle, its exact source
packet, explicit premise-root handles and a conclusion-root handle. The caller
chooses what is being assumed; the checker does not assert every graph root.
Predicate-application occurrences become opaque Boolean atoms. Shared handles
are shared atoms; matching words or labels never silently unify distinct ones.
Conditional nodes are interpreted as material implication explicitly, with
negation and conjunction as Boolean operations.

The checker first establishes whether any assignment satisfies the premises.
If none does, it reports `inconsistent_premises` rather than advertising every
conclusion as proved. Otherwise it distinguishes `entailed`, `contradicted` and
`undetermined`, returning relevant assignments. It enumerates at most 12 atoms
and 4,096 assignments. Exceeding limits is a separate result. Quantifiers,
attributed/quoted/hypothetical contexts, external Claim premises and unsupported
relations are refused rather than stripped away.

This directly addresses a report error: UNSAT means unsatisfiable, not
undecidable. A successful formal check remains conditional on the supplied
interpretation. An intentionally wrong interpretation with exact source quotes
can pass; the source-fidelity flags stay false. The module never promotes a
candidate or rewrites canonical data. Its API, bounds and runnable example are
in `../../loom/tools/structure/LOGIC_CHECK.md`.

## Cheap scoring without forcing a single topic

The score replayer receives only an existing `prepare_context()["packet"]`, a
bounded allowlist of Claim IDs, an explicit question/rubric and a saved response.
It never reads the full undo/audit snapshot. Request identity binds packet
content, candidate selection/order and question/rubric content.

Each candidate has an independent probability. Two projects can both receive
high scores; probabilities are not normalized into mutually exclusive choices.
Caller-supplied thresholds produce `keep`, `review` or `drop` suggestions. These
are context priorities, not instructions to delete graph records. Missing or
invalid individual scores remain visible as unknown/review. A stale envelope,
unknown ID or duplicate row rejects the response while preserving candidates
for review. Original Assessments and source handles remain intact.

The comparison function accepts an explicit bijection between two candidate
sets and reports suggestion agreement, measured-pair coverage and score drift.
It does not treat missing scores as zero or assume that differently named
candidates mean the same thing. A stable result may still be wrong. Usage is in
`../../loom/tools/structure/CONTEXT_SCORES.md`.

Jev is a plausible future adapter for this protocol, but it uses a different
endpoint from the existing chat-completions helper. Its official documentation
describes limits on indirection, numerical operations, long irrelevant context
and consistency across equivalent questions. It also says English is strongest.
Hence a live adapter needs its own explicit configuration, versioned evaluation
and Polish/English quality measurements; merely entering a Jev model name into
the current generative-model field would not implement that protocol.

## Evidence and next useful measurement

The original 213 research mechanism checks passed after recovery. The final
combined research suite passes **260/260 tests** in **1.221 seconds**: 213 prior
checks, 13 new logic author tests, 21 score author tests and 13 independent test
methods. The independent first run passed before a later source-review fix to
the wrapper/preflight rejection order; the original report is preserved and
the same suite passes against the final version. Detailed protocol/results are in
`GEMINI_EXPERIMENTS_INDEPENDENT_2026-09-28.md`. These are supplied-structure and
saved-score contract checks. They do not resolve the historical catalog recall
failure (13/45), demonstrate better model extraction, or measure paid inference.
No native code, schema, UI or canonical persistence changes are part of this
increment; the prior native build result is historical and was not rerun.

The next informative live experiment should hold candidate retrieval and total
budgets constant while comparing raw source context, a graph-derived view, and
a view with bounded source refresh. Independently label late project starts,
returns after distractors, overlapping topics, corrections and ambiguous
references. Separate shortlist recall from scorer quality, source-to-graph
accuracy from graph reasoning, and abstention coverage from correctness. Keep
the first run, including regressions, before tuning. No provider is selected by
these offline results alone.
