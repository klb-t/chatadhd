# Conversation extraction: offline readiness, 2026-10-04

**No model/provider call was made.** The negative source-only first run remains
the primary result. This increment adds a reproducible public-archive replay,
separate source-binding diagnostics and a three-arm source-only request preview.
It does not complete a live extraction-quality study or certify a production
pipeline. No private export, `eval/real-holdout-key` or sealed validation was read.

## Reproduced first results and the separate decoder

The replay reads the immutable public source at
`af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`, verifies ZIP SHA-256
`5571ab4ff59c85b23bd32f9733df68495155998666203394120a1b99632530ef`,
checks saved response/ledger billing and recompiles with the unchanged strict
compiler. Exact current input, gold and fixture-manifest bytes must match their
archived SHA-256 snapshots before replay; gold labels remain uninterpreted until
first outputs persist. Per-case alias/gold drift cannot be hidden by an aggregate
score tie. All 24 compiled outcomes equal the archived first snapshots. Historical
primary scores and the existing secondary decoder scores match exactly.

| Axis, with preserved denominator | Strict original decoder | Existing exact-turn-ID projection |
|---|---:|---:|
| Planned conversations | 24 | 24 |
| Compiler-available graphs | 18/24 | 18/24 |
| Strict reference source assertions: TP / FP / FN | 6 / 43 / 54 | 38 / 11 / 22 |
| Strict reference source assertion precision | 6/49 | 38/49 |
| Strict reference source assertion recall | 6/60 | 38/60 |
| Strict reference used-node alignment: TP / FP / FN | 45 / 15 / 15 | 45 / 15 / 15 |
| Strict status events: TP / FP / FN | 0 / 6 / 6 | 0 / 6 / 6 |
| Accepted assertion records | 8 | 46 |
| Accepted status-event records | 1 | 6 |
| Exact quote/span/source-time checks on accepted records | 9/9 | 52/52 |
| Original usage-reported USD | 0.0263512 | Same original calls |
| New calls / new cost USD | 0 / 0 | 0 / 0 |

The projection converts 45 exact, uniquely resolvable raw turn-ID evidence strings
to `{turn_id}` objects. It preserves every previously accepted typed record,
every unavailable graph, source bytes and original model objects. It does not
salvage four duplicate-key responses or two truncated outputs. The apparent
strict-reference improvement isolates a **historical decoding loss**; it is
neither a new model result nor a v2/v3 response. Both decoders and their scores
remain visible. No existing gate was relaxed and no graph was promoted.

These strict scores are representation-dependent lower bounds under the frozen
surface-alias matcher. They are not a world-truth or semantic hallucination rate.
Byte/span/time integrity proves that a citation binds to source text; it does not
prove the text expresses the relation, that attribution/correction authority is
valid or that content is true. The projected accepted assertions still include
eight source-invalid records according to the preserved, nonblind manual audit.

The separate manual audit reviewed **62 raw assertions**, classified **38
source-valid, 10 source-invalid, 14 requiring review**, and checked **62/62**
citation/time bindings. Its **38/62 source-valid count is not recall 38/60**.
The two numerators happen to agree for different measurements. The manual audit
was written after outcomes by a fixture author and remains secondary; it cannot
replace or retune the primary score. Nineteen source-supported node expressions
were outside the bounded reference inventory.

Machine receipts and complete derived outputs are under
[`extraction/replay/`](extraction/replay/). Original first responses and negatives
remain in the immutable source archive; the new replay does not overwrite them.

## Next controlled comparison: contract, semantics, provenance

Preparation uses only the existing 24 public authored DEV input conversations.
Reference inventory, aliases, gold, family labels and manual annotations never
enter model bodies. All arms use the same ordered raw `id/source_id/turns` payloads.
The original system prefixes, model/provider, temperature and output allowance
are retained as historical presets. Optional caller request settings are data;
changing them requires a separately identified condition, not inheriting these
default-arm results. The driver imposes no new model/output budget ceiling.

| Arm | Sole prompt difference | Primary hypothesis | Historical reservation USD |
|---|---|---|---:|
| Source-only v1 baseline, 24 requests | Original recipe | Same-input fresh comparator, optional | 0.1162048 |
| Grammar v2, 24 requests | Append generic literal output grammar | Fewer contract-shape failures | 0.1252768 |
| Semantic v3, 24 requests | Append generic semantic contrasts to unchanged v2 prefix | Fewer operand-negation, relation-polarity and report/nonendorsement errors | 0.1367008 |

These **72 proposed requests are previews**, not execution manifests or transmitted
requests. The existing v2/v3 recipes account for 48 of them and remain unexecuted.
The optional new baseline costs an additional planning reservation of 0.1162048;
the 48 recipe requests reserve 0.2619776, and all 72 reserve 0.3781824. These are
old byte-based estimates with historical price assumptions, not current quotes,
account balances or money spent. They are separate from the 432-request study;
there is no automatic addition to that study or to an authorized allowance.

Before live work, obtain owner approval, reconcile current key/account identity,
usage, unknown charges and outstanding reservations, refresh provider/pricing
capabilities and use the existing nonresetting shared budget. The present scope
provides no paid execution capability. The owner's current no-paid-call-without-
approval instruction controls this work even where historical docs differ.

For each live arm, persist exact first responses and compiled source-only graphs
before loading DEV reference labels. Keep all 24 planned case denominators,
including missing, duplicate-key, truncated and malformed outcomes. Report:

- **Contract axis:** response availability /24, duplicate keys, length/refusal,
  invalid record/evidence shapes, attempted/cost-known/cost-unknown counts.
- **Semantic axis:** unchanged strict used-node/assertion/event metrics with
  denominators 60/60/6; per-case error changes. Independent clause review of
  unmatched expressions, attributed speakers, operand versus relation negation,
  silence/nonendorsement and same-actor correction remains separate.
- **Provenance axis:** source-payload/model-object hashes, recomputability,
  exact Unicode and UTF-8 spans, source time versus extraction observation time,
  source history retention. A passing binding is not a semantic TP.

V1→v2 isolates the grammar intervention; v2→v3 isolates the semantic suffix.
Evidence projection remains a separate decoder ablation of the same saved raw
responses, never a substitute for a response to another prompt. Full-conversation
extraction is retrospective: filtering its later graph by source time does not
establish causal-prefix independence. There is no independent validation claim
from this already inspected, correlated DEV panel.

## Local reproduction

Run from a clone retaining the pinned public historical commit, into new output
directories (existing destinations are refused):

```sh
python -B -m loom.tools.structure.extraction_readiness_v1 prepare \
  --output NEW_SOURCE_ONLY_PREVIEW
python -B -m loom.tools.structure.extraction_readiness_v1 replay \
  --output NEW_FIRST_RESPONSE_REPLAY
python -B -m unittest loom.tools.structure.test_extraction_readiness_v1 -v
```

`prepare --request-options FILE.json` preserves source-only messages while
previewing caller-selected request options. When options change, the historical
reservation is explicitly unavailable; no fabricated new-model price is shown.
No network, key access, paid runner or native graph write occurs in either command.

The nine new synthetic regressions pass. They test source/gold isolation,
uncapped caller settings, preserved unavailable-case denominators, refusal of
source/span drift, alias/gold fixture drift, immutable public artifact access and
ZIP hash/path handling.
They are **mechanism evidence, not model accuracy**. Initial harness/ZIP-boundary
failures are preserved in
[`extraction/MECHANISM_FIRST_FAILURE.md`](extraction/MECHANISM_FIRST_FAILURE.md).
The parent workstream performs the complete repository test gate.
