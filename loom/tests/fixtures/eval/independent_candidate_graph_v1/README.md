# Independent candidate-graph contract pilot v1

This pilot contains 32 manually source-grounded cases: 16 development and 16
fresh validation, each with eight English and eight Polish sources. Gold direct
Entity/Claim drafts and grounded-anchor/frame encodings describe the same source
interpretation. No model is asked to discover it. This measures representability,
validation, compilation, source integrity and comparison-view preservation, not
live-model parsing, truth, recall on owner history or cost of model use.

The method authors received only public schema coordination and aggregate case
counts. They did not receive these examples, labels or outcomes. The independent
authors did not inspect method-author tests, `synthetic_dev`, or any real holdout
key. Initial labels and inputs were frozen before executing either method.

## Frozen data

| Split | Canonical self-excluding SHA256 |
| --- | --- |
| Development | `cb14ad33834be118def4a2305f503c58e5dac3a91cb20dcbf5223bac3b99501b` |
| Validation | `4dcb59baaccd6deb914c5504004649a5e2f926183b69b0797e43ec654a273787` |

Hashing uses sorted-key compact JSON, UTF-8 without ASCII escaping or a final
newline, nonfinite numbers forbidden, and excludes only `frozen_sha256`.

One development schema correction occurred before any method execution: the
published compiler contract clarified that `coverage.reason` must be nonempty.
Fourteen empty reasons were replaced with a descriptive manual-gold reason and
the packet-bound anchor hashes recomputed. The source bytes, graph labels and
expected outcomes did not change. The superseded development payload hash was
`b8c4aeb940a43807dd48426df9b9d51780b10189616c9d7ce2ff99d90845a420`.
Validation already met that requirement and was unchanged.

Each split contains 13 intended represented cases, two valid explicit abstentions
and one deliberately dangling reference to reject. Both encodings include
predicate applications, conditionals, negation, conjunction and quantification;
the contrasts vary operand order, condition direction, negation scope,
quantifier kind/order, shared binding and quotation context. Conjunction order
is source representation order, not a claim of changed classical truth.
Seven development and six validation contrasts are predeclared.

`source_packet` uses native-shaped located Observations and explicit existing
Entity references where needed. Raw source bytes remain outside that packet in
the fixture. Prior Claim arrays are empty in this pilot: eligibility and
interpretation of old assessed context are not independently measured here.
Whole-sentence support is deliberately coarse and does not demonstrate that
models find minimal support spans. `VALIDATION_PROTOCOL.md` records the separate
validation author's process and fixture-only checks.

## Independent checks

The evaluator checks raw source hashes, original-source Observation locations,
every supplied observation-local support span and the public anchor-hash
derivation before loading a method. It independently expands the public frame
syntax and verifies that the supplied A and B gold have the same Claim content.
This expansion is separate from either implementation.

Compiled fields are reconstructed against immutable pre-call source gold:
entity kinds/attrs, rewritten endpoint and scope identities, literal values,
port/ordinal/polarity/context, exact support spans and premise references. Added
locators and text hashes are checked separately. Original local handles cannot
pass as resolved compiled references. Every method invocation receives fresh
input copies. Agreement between A and B is reported separately because both
use the same compiler and could otherwise share an undetected loss.

Valid unknown-only output is counted as abstention, not represented success.
Rejected input must retain evidence and keep persistence/inference disabled.
Copied source alone is not evidence that the comparison graph retains structure.
No complete Assessment, canonical ID, proof eligibility or native promotion is
inferred from successful serialization.

Reproduction and measured method hashes are in `INITIAL_RESULTS.md`.
Future method revisions must use separately labeled reports with these frozen
inputs unchanged; any tuning informed by outcomes requires fresh validation.
