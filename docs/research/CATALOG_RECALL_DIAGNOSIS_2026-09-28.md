# Catalog recall diagnosis — 2026-09-28

The existing development gate still selects **13 of 45 relevant conversations**
(recall 0.288889; required minimum 0.55). This diagnosis found a coverage gap for
unnamed project continuations and owner-related reasoning, not a demonstrated
source-loss bug that could responsibly be fixed by a small catalog patch.
Production code, fixture labels, thresholds and weights are unchanged.

Only the public `loom/tests/fixtures/eval/synthetic_dev` fixture, its gate,
catalog implementation and policy were inspected. Neither the owner holdout key
nor independent research fixtures were inspected. These are development-set
observations, not independent accuracy estimates or provider-quality results.

## Reproduction and measured evidence

From the repository root, using the already-built native test executable:

```sh
LOOM_CATALOG_EVAL_VERBOSE=1 loom/build/dev/loom_tests --test-suite=catalog_eval --no-colors
```

The command intentionally exits 1: 73 of 74 assertions pass; recall is the only
failed assertion. This task did not build native code or rerun the full suite.

| Measurement | Result |
| --- | ---: |
| Relevant conversations selected | 13/45 |
| Labeled conversation precision | 13/13 = 1.0 |
| Noise traps selected | 0/5 |
| Generic noise selected | 0/15 |
| Auxiliary documents selected, outside conversation labels | 3/3 |
| Units scanned | 68 |
| Missing source for identity verification | 0 |
| Score labels: relevant / candidate / irrelevant | 9 / 7 / 52 |
| Links / vocabulary expansion terms | 56 / 1 |

The score-label and expansion counts describe this executable, and differ from
the earlier checkpoint in `docs/CATALOG_QUALITY_2026-09-28.md`; final selection
remains 13/45. Score labels are not final selection decisions.

Every one of the 32 missed conversations has readable source
(`identity_source_available=1`), zero verified identity-alias hits, zero principle
hits, and zero title, version, code-evidence and project-membership features.
Their scores range from 0.0544 to 0.2512, below the existing candidate threshold
of 0.35. Nevertheless, 25 have nonzero self-profile BM25, all 32 have nonzero
philosophy BM25, and 25 receive some link contribution. Retrieval is observing
topic resemblance; it lacks sufficiently discriminating context for selection.

A separate diagnostic inspected titles and all message text in both public ZIP
exports against the full aliases configured by `persona_aliases()` in the gate.
None of the 32 misses contains a configured full alias as a folded, bounded
surface phrase. That diagnostic used Python Unicode folding and is supporting
source inspection, not a replacement implementation of the native alias
matcher. The native zero-hit feature counts are the authoritative measurement.

Examples already present in the public fixture illustrate the missing signal:

| Public source ID | Content, paraphrased | Missing link to configured profile |
| --- | --- | --- |
| `nf-02-storage` | Storage alternatives and an earlier brainstorm | No project name; a continuation reference needs antecedent context |
| `nf-04-sync-v1` | Synchronization alternatives using Git, CRDT or WebRTC | Feature discussion without the configured project alias |
| `nf-05-transcription` | Voice-note transcription and provider alternatives | Capability discussion without the project name |
| `wd-03-v01` | Watcher/restart implementation and heartbeat | Code and version evidence without verified prose identity |
| `lk-02-strategy` | A named person and letter-versus-court alternatives | Partial referent, not the configured full case-name phrase |

These IDs locate evidence for diagnosis only. They must not become selector
features, lexical exceptions, training examples for a claimed independent score,
or automatic project-affiliation assertions.

## What the implementation explains

`loom/src/catalog/extract_text.cpp` extracts messages for both providers;
`score.cpp` rereads source to verify identity evidence. No missing-source signal
explains these misses. Exact aliases intentionally require word boundaries;
restoring arbitrary substrings would reintroduce documented false identities.
An alias inflection policy is a separate question from unnamed continuations.

The configured profile contributes aliases and principle phrases. BM25 operates
on normalized sketch terms, while vocabulary expansion starts from score-labeled
relevant units and requires repeated, discriminating terms. Link propagation
adds one damped neighbor contribution and requires the target to have its own
retrieval evidence. These mechanisms do not resolve an anaphoric reference such
as an earlier brainstorm, establish which unnamed feature belongs to which
project, or distinguish project membership from a reusable reasoning pattern.

The gate supplies fictional project aliases through `ProfileConfig.extra_terms`.
That is a limited but intentional profile configuration. It does not supply a
grounded concept/identifier history for each project. The test header describes
“linked-unit completion”, but the test executes scan, profile, score and select;
it does not execute catalog import or a separate referenced-conversation
completion stage. The result is a selector measurement under this configuration.
This scope mismatch in the comment is not grounds to weaken the unchanged gate.

There is also a **latent project-metadata gap**, distinct from this result:
`scan.cpp` does not populate `CatalogUnit.project_ext_id`, although same-project
link scoring and `ImportOptions.include_project_siblings` can consume it. The
public conversations have no corresponding project/gizmo metadata to extract.
Populating genuine provider metadata should eventually have a source-preserving
regression test, but cannot explain or repair the measured 32 misses here.

## Next source-grounded experiment

Use an unnamed continuation as a concrete semantic-pipeline case. For example,
take the exact source observation for `nf-02-storage`, retrieve a bounded set of
earlier observations using available explicit references and grounded feature
evidence, and provide only allowlisted prior graph context with its existing
assessments. Preserve exact source spans and record which context was available.
Ask the candidate pipeline to represent the discussed alternatives and any
proposed continuation relation, including ambiguity or insufficient evidence.

Measure separately whether the needed earlier observation entered the source
packet, whether its bytes and reference identities survived, whether the
candidate structure preserves the alternatives, and whether source selection
improves without admitting unrelated conversations. A model-proposed affiliation
or continuation remains a candidate with provenance; it does not become an
observed project-membership fact or an inference premise automatically.

Before integrating a selection change, use newly authored cases covering an
explicit continuation, a genuinely unnamed continuation with corroborating
context, two projects sharing the same feature vocabulary, a nearby unrelated
conversation, and an ambiguous antecedent that requires abstention. Evaluate
retrieval coverage and false inclusion separately from structural preservation.
Do not tune weights or add terms taken from this gate's relevance labels.

## Reproduction identities

SHA-256 values from the diagnosed executable and inputs:

| File | SHA-256 |
| --- | --- |
| `loom/build/dev/loom_tests` | `e7ebc7b9080b5671037485e446662903fcffca4d74c5ba7131dd024a94efa442` |
| `loom/tests/test_catalog_eval.cpp` | `7b3a7bd5e5b1d2b2680fd333beae379ffd85ff3fbfcb6635d25afed641d660fc` |
| `synthetic_dev/chatgpt_export.zip` | `318dc28ec02f4cd2871a7b5b0b057096e029086a1f96546ad965e6a9c1332637` |
| `synthetic_dev/claude_export.zip` | `167af67d709e577aa7e748ed7c00a0ba31f44e55fe808051e7c8bd28246fa299` |
| `loom/data/policy/relevance.json` | `84c2b1f45b6b3da0d1ee875d94f2853a506eae641bf30da860192e5810bbe552` |
| `loom/data/policy/selection_rules.json` | `b98da5d1c509e8bbafee8f13fc6c5fe4230217bb14300ffc1281803109189685` |
