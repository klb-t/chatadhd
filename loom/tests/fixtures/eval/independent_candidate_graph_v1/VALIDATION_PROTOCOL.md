# Independent candidate-graph validation fixture

`validation_cases.json` contains 16 independently authored validation cases:
8 English and 8 Polish, using ecology, education, and music. Development sources,
method implementations, author tests, earlier example fixtures, and the real
holdout key were not read. No paid model calls or extraction method runs were
used to author or label these sources. Sources and structure labels were manually
specified by the independent evaluation agent; they are synthetic evaluation
material, not human annotations of natural archive documents.

The public packet, direct candidate graph, grounded-anchor, and grounded-frame
schemas were relayed by the independent evaluator before final construction.
All source texts, graph annotations, controls, contrasts, and hashes were fixed
before either method ran on this fixture. Do not tune this file after outcomes.

## Freeze

Full-file SHA256 of `validation_cases.json`:

`0407395230790aaddd9cd2768c81a0b56ac72f401b469bdff6b29d06662d9491`

Self-excluding canonical `frozen_sha256`:

`4dcb59baaccd6deb914c5504004649a5e2f926183b69b0797e43ec654a273787`

The latter hashes sorted compact UTF-8 JSON with the root hash field omitted,
using `ensure_ascii=False`, `allow_nan=False`, and no terminal newline. The full
file hash includes formatting, the hash field, and final newline.

## Coverage and expected behavior

The supported family counts are two predicate argument-order cases, three
conditional/negation-scope cases, one conjunction source-order case, four
quantifier scope/binding cases, and two conjunction/negation-scope cases.
A quoted conditional is a represented quotation-context control. An ambiguous
pronoun and an unsupported exactly-three quantifier produce valid empty candidate
graphs with grounded unknown/coverage records. A dangling-reference control has
an intentionally packet-absent native Entity target and requires rejection.

Thus 13 cases intend an accepted representation, two intend valid abstention,
and one intends rejection. Every case requires all eligibility gates to remain
false and source data to remain unchanged. `coverage_status` records the supplied
coverage status; `accepted_representation` and `represented` indicate the intended
accepted result. The dangling case deliberately submits represented coverage but
has both accepted-representation flags false.

Six contrasts are predeclared. They distinguish predicate argument roles,
consequent negation, outer versus inner negation, quantifier nesting order, shared
versus distinct binder references, and whole-conjunction versus first-member
negation. Conjunction member order is source order; no logical inequivalence is
claimed merely from swapping conjuncts. Distinct binder nodes do not imply that
their variables must take distinct values. An unused quantified variable remains
represented when the source explicitly introduces it.

## Grounding and construction

Each packet contains native located Observations, native Entities, and an empty
prior-Claim list. Native Entity identities are supplied for constant and predicate
terms, with explicit `denotes` links in the candidate graph. Consequently this
validation half exercises allowlisted native Entity links but does not evaluate
prior-Claim premise handling.

Each graph assertion has an exact whole-sentence support span relative to its
Observation text. The native Observation locator independently points into the
complete raw source. Raw records include CRLF, a leading tab, Polish diacritics,
Greek characters, an em dash, and typographic quotation marks. Whole-sentence
support is deliberately coarse: exact-byte grounding does not prove the proposed
structure, disambiguation, or world proposition.

Direct graph drafts were authored from the manual semantic gold. The compact
representation uses the same independently specified anchors and semantics,
encoded through the frozen public frame/link contract. Stage 1 carries coverage
and unknown records; stage 2 does not add duplicate coverage. No method expansion
code was consulted or executed. This is a controlled serialization and validation
comparison; it does not measure spontaneous language-model decomposition, entity
retrieval, source understanding, or the cost/quality of generating anchors.

Negation is explicit operator topology. Positive qualifiers on structural links
say the source structure is being represented, not that its embedded world
proposition is positive, established, or proof eligible. Quotation remains in a
quotation scope with quoted assertion context throughout.

## Pre-method integrity check

Fixture-only checks passed for 16 cases, 143 Entity drafts, 365 Claim drafts,
48 frames, and all 526 checked span occurrences. Checks covered source hashes,
UTF-8 locator alignment, packet/anchor hashes, candidate reference closure except
the intended dangling target, single `in_scope` incidence, scope-parent references,
nearest visible same-symbol binding, contiguous port ordinals, coverage references,
and graph content agreement with a separate expansion of the public frame
contract. No author implementation, author tests, native build, matcher, or paid
model endpoint was invoked.
