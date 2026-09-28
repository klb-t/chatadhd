# Frozen source-text pilot for live extraction

This is a fresh, small diagnostic pilot for actual model responses, prepared
before any provider inference. It contains **32 authored cases**, equally split
between English and Polish. Sixteen composition families each contain the two
language versions. Eight families (16 cases) form development and eight other
families (16 cases) form validation. Primitive operations and some vocabulary
are intentionally shared across the split; this is family separation, not a
claim of complete linguistic independence or representative conversation data.

All prompt-visible case, snapshot, unit and member identifiers are opaque.
Family names appear only in the offline manifest. An initial pre-inference
freeze used descriptive IDs; they were replaced before any model call to avoid
revealing expected structure through metadata. The manifest records the prior
digest and the reason. Source text and semantic labels were unchanged.

Inputs contain source text and ordinary `loom.source_packet/1` records. Gold is
stored separately. The model must produce the existing
`loom.candidate_graph/1` bundle. The evaluator derives a disposable operation
projection from the graph; no second authoritative graph, database schema or
promotion pathway is introduced.

## Files and frozen identity

All fixture files are under
`loom/tests/fixtures/eval/live_structure_pilot_v1/`.

| File | SHA-256 at pre-inference freeze |
|---|---|
| `inputs.dev.json` | `01c544aa3937152b6f698a1a8f509bf831c8bb4d02126b54769e1cf97f198085` |
| `inputs.validation.json` | `78e52712d8badd57615efbaa41d415eaca866c6b83d314b9ac4595c7fa841381` |
| `gold.dev.json` | `8c1318969d99e615d043648ee8788fbd8970271d2b5f1cb5d513fd4fe66b7c78` |
| `gold.validation.json` | `3bd040c23eb518152288a3843ae5c48ecc5d1d029ec2103f706c8f60dbc0f49d` |
| `manifest.json` | `8ad989b50a164ad830dc8df62481cb2ac71949001dde10cdf4cff48bcc39fa07` |

The manifest records family, language and split without semantic labels, plus
all four data-file digests. It was frozen before any model call or any validation gold disclosure to the
method author. The source/gold author did not inspect
existing hidden holdout keys, existing evaluation validation labels or provider
responses. The author did read public compiler documentation and author tests
in order to target the supported representation. Gold changes after inference
require a new pilot version; the original labels and first-run results must
remain available.

Input files are arrays of `{case_id, language, source_packet}`. Gold arrays are
matched by `case_id` and also bind the exact canonical packet SHA-256. They
contain expected anchored projections and explicit abstention expectations.
An orchestrator may load gold only after the request/response is fixed. Gold
must never enter a prompt, few-shot example, provider metadata or retry feedback.

## Scoring contract

`loom/tools/structure/live_pilot_score.py` exports:

```python
score_case(case_input, gold, bundle) -> dict
```

The bundle is parsed response JSON, with no repair performed by this evaluator.
The report separates:

- Envelope shape, full compiler-contract validity and independently checked
  exact UTF-8 source support.
- Represented versus located abstention; an abstention is never counted as
  represented extraction.
- Structural exactness after erasing lexical anchors, retaining ordered roles,
  operation nesting, quantifier kind/binding and asserted/quoted context.
- Anchored semantic exactness, additionally requiring predicate and argument
  roles to resolve to the correct source locations.
- Final strict semantic exactness, additionally rejecting unexplained unknowns
  or nonpositive coarse Claim polarity in these explicit-negation fixtures.

Local handles, labels and term symbols do not identify meaning for scoring.
A term resolves to a gold anchor only when its exact support contains source
locations for one unambiguous anchor. A full-sentence span containing several
possible roles therefore cannot impersonate precise role grounding. Source
quotes may cover a short phrase around the anchor. Bound variables are compared
by their quantifier position, not their chosen printed symbols.

Support correctness means bytes match the source; it does **not** establish
that the model's interpretation is right. Role reversal can pass schema, support
and structural shape while failing anchored semantics. Polarity, conditional
direction and quote attribution are separate failure opportunities.

The pilot includes four ambiguous cases. For these specific whole-passage
cases, the frozen policy requires a source-located unknown covering the whole
short passage and no guessed graph. Correct partial extraction is a useful
future capability, but is not interchangeable with that strict pilot label.
Prompts must make whole-passage abstention available. This policy must not be
silently generalized into the application's extraction behavior.

## Intended prompt and reporting discipline

Use source agent/subject as argument zero and patient/object as argument one.
Preserve conjunction source order. Represent negation explicitly, conditional
antecedent and consequent separately, and forall/exists with explicit binding.
A quoted passage is quoted rather than endorsed. Predicate/constant term
supports should be short and identify the exact source words. UTF-8 token
location tables may be derived mechanically from inputs without gold.

The scorer accepts one response bundle. Competing readings belong in separate
existing-contract bundles, but this pilot's unresolved passages expect located
abstention. It does not select the best of several attempts. Keep the first
response and all refusal, invalid-JSON, truncation, timeout and contract failures
in the denominator. Aggregate development and validation separately, and show
represented-case extraction separately from appropriate-abstention accuracy.

A strict projection may reject logically equivalent rewrites, for example
De Morgan transformations, swapped conjunction order, or an alternative
quantifier restriction encoding. Scores measure fidelity to a declared small
representation grammar. They are neither logical-equivalence proofs nor broad
NLP accuracy. Quotation-marker phrases are framing metadata, not separately
scored predicates. Topic-boundary detection, real archive contexts, nested
binding, paraphrase invariance and coreference resolution need later pilots.

## Mechanical verification, not model quality

```sh
python -m unittest discover -s loom/tools/structure -p 'test_live_pilot_score.py' -v
```

Result on 2026-09-28: **10 tests passed**, including all 32 fixture projections,
source/gold identity and freeze digests, role reversal, conditional reversal,
quoted-to-asserted mutation, forall-to-exists mutation, opaque handle renaming,
misleading full-passage term support, UTF-8 quote forgery and abstention handling.
The tests construct candidate bundles from gold solely to verify mechanical
consistency. They do not extract structures from text. No live provider calls,
latency, cost or extraction-accuracy measurements are claimed here.
