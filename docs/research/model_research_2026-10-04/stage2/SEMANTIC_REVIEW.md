# Stage 2 source-only semantic preregistration

`semantic_reference.source_only.v1.json` contains **15 inspected synthetic DEV
conversations and 48 source-interpretation criteria**, authored in this session
before Stage 2 provider-output collection. It is not independently authored gold,
world-truth evidence, an unseen benchmark, or measured model quality. A second
agent independently reviewed the annotation against the same source inputs;
that review does not change the reference's provenance class.

The source-only inputs, recipe bytes and 60 prepared requests remain unchanged.
The frozen config's earlier pending-reference marker remains historical; this
separate reference and its freeze record supply the additional preregistration.
No existing synthetic answer key, holdout, private-account/key material, or model
output was used to author or review this annotation. No model calls were made
by this lane.

Every criterion retains observation IDs, exact nonempty UTF-8 spans, and the
original source pointers, speakers, node IDs and order. Span offsets address the
Observation text, not the archive; null archive offsets remain null. Full-turn
support is deliberate where chronology, affirmation or quotation depends on
surrounding wording. Every supplied observation appears in at least one criterion.

Assess interpretations, not exact graph handles, labels or one graph template.
Equivalent predicate applications, scoped expressions and located abstentions
can preserve source meaning in different ways. Source-grounded atomic predicates
are allowed; the absence of a dedicated temporal or alternative operation does
not prohibit every atomic representation. Located partial/unknown coverage is
required for the meaning or structure that the actual graph loses, and an atomic
label does not establish measured structural sequence accuracy.

In particular, an ordered confirmation gate allows both a procedural
prerequisite-to-permission conditional and an event-to-prior-confirmation
necessary-condition reading. Confirmation does not itself prove a render/upload
occurred. Before/after order needs preserved source context or a located limitation;
operand ordinal alone is not a temporal relation. Natural-language generic rules
may support grounded quantification, but this sample has no explicit symbolic
variable or shadowing test. Do not invent a binder-performance denominator.

## Manual assessment and aggregation

Review every first response and every returned alternative bundle against its
case criteria and the generic rubric. Retain exact response bytes/hash and the
native validator evidence separately. A judgement must include a rationale,
candidate JSON pointers and exact source spans; a pointer/quote alone is not a
semantic judgement. Reviewers must inspect context, polarity, gate meaning,
version chronology, quotations and any claimed binding. Do not select a favored
alternative or replace failed first responses with retries.

`loom.tools.structure.stage2_semantic_review_v1` validates reference spans and
manual record shape, then aggregates the manually supplied judgements. It does
not decide semantic correctness, execute the native validator, inspect response
file contents or independently verify declared response hashes/native validity.
The dispatcher/reviewer must bind those declarations to retained first-response
and validator evidence. The scorer has no case-ID branches, prompt strings or
automatic label matching; all expected/forbidden meanings live in reference data.

The review-file shape is:

```json
{
  "records": [{
    "request_id": "prepared request ID",
    "request_hash": "prepared request hash",
    "response_sha256": "64 lowercase hexadecimal characters",
    "native_contract_valid": true,
    "bundle_count": 1,
    "reviewer": "reviewer identity",
    "criteria": [{
      "criterion_id": "reference criterion ID",
      "bundle_judgements": [{
        "bundle_index": 0,
        "judgement": "satisfied",
        "rationale": "The inspected interpretation preserves the stated meaning.",
        "candidate_pointers": ["/bundles/0/roots"],
        "source_support": [{
          "observation": "supplied observation ID",
          "byte_start": 0,
          "byte_len": 1,
          "quote": "exact source substring"
        }]
      }]
    }]
  }]
}
```

Valid judgements are `satisfied`, `violated` and `unresolved`. Every alternative
must satisfy a criterion for the request criterion to be satisfied; any violated
alternative is a violation. Missing alternatives, absent judgements, empty
bundles, missing responses and native-invalid outputs remain unresolved. The
denominator comes from the frozen prepared inventory: **60 request slots and
192 criterion slots (48 criteria across four methods)**. Availability, native
validity, semantic agreement, extraction coverage, tokens, latency and known or
unknown costs remain separate quantities. A located abstention preserves an
evidence boundary but does not count as successful extraction of that criterion.

```bash
python3 -m unittest loom.tools.structure.test_stage2_semantic_review_v1 -v
python3 -m loom.tools.structure.stage2_semantic_review_v1 \
  --reference docs/research/model_research_2026-10-04/stage2/semantic_reference.source_only.v1.json \
  --inputs docs/research/model_research_2026-10-04/stage2/inputs.synthetic_dev.json \
  --prepared docs/research/model_research_2026-10-04/stage2/prepared/prepared.json \
  --reviews PATH_TO_MANUAL_REVIEW_JSON
```

Omit `--reviews` to verify spans/dependency hashes and report all planned semantic
slots unresolved. Four mechanical regressions cover exact UTF-8 occurrence
spans, repeated quotes, absent judgement/abstention, all planned denominators and
review of all alternatives. These tests are not semantic-quality measurements.

Before comparing arms, hash-bind reviews to this reference/freeze and retain the
reviewer record. Interpretation agreement on this inspected DEV sample does not
establish world truth or improvement over W1's different end-to-end scorecard.
