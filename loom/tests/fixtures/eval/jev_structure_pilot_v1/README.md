# Jev structural classification pilot v1

This frozen authored corpus contains 64 short states: eight structural families,
four variants per family, and Polish/English versions of each. All states use the
same twelve binary Noul questions. The questions describe relations, quantifier
scope and reasoning roles, not topic keywords. No model calls were made.

Only each input row's `state` and `questions` belong in a model prompt. The opaque
`case_id` and `language` identify the request; they contain no family, variant,
split or answer names. `gold.json` and `manifest.json` are evaluator-only. Do not
send labels, invariance groups, sibling texts or expected changed questions to a
model. Question descriptors are fixed public instructions, not per-case hints.

`inputs.json` is a JSON array of {case_id,language,state:{text},questions:{q01:...}}.
Every question has {type:"noul",instructions,criteria:{true,false}}. `gold.json`
contains binary 0/1 labels and evaluator-only family/split/variant/group metadata.
Labels are supplied for all twelve questions; no omitted label means false.

The four variants are base, same-domain paraphrase, cross-domain transfer and a
same-vocabulary structural foil. A family contains six equivalent base/paraphrase/
transfer texts across the two languages. Its two translated foils are equivalent
to one another and differ from the base only on the declared question subset.
The manifest records exact group membership and changed-question expectations.

Development families: conditional direction, inductive generalization,
support versus counterexample, causation versus correlation. Validation families:
universal versus existential, negation scope, analogy versus identity, conjunction
versus disjunction. Entire families, their translations and all variants stay in
one split (32 states each). Freeze model/prompt/settings and evaluation thresholds
before validation inference; repeated tuning against validation requires new data.

## Structural counterexamples and distinctions

- Reversing antecedent and consequent changes q01/q02 despite exactly the same
  content vocabulary. P and Q are local proposition roles, not semantic keywords.
- A finite sample offered as support for a universal conclusion changes q03/q05
  relative to a conclusion restricted to an examined individual. Both retain q07.
- An existential main assertion changes q03/q04 relative to a universal one.
  Named individuals and finite observations do not count as explicit existential
  quantification; quoted universal targets do not count as endorsed rules.
- Not-(P-and-Q) changes q06 relative to (not-P)-and-Q. q12 asks specifically for
  asserted positive inclusive disjunction and does not apply De Morgan closure.
- A confirming case supporting unendorsed target T has q07; a violating witness
  refuting the universal T has q08. A quoted hypothesis is not endorsed merely
  because its sentence occurs. Inductive support is not deductive proof.
- A mapping between distinct systems has q09; declaring the descriptions to name
  the same system has q10. Shared topic words establish neither relation.
- Causal assertion has q11; co-occurrence explicitly withholding causation does
  not. This scores the represented assertion, not real-world causal truth.
- P-and-Q changes q12 relative to inclusive P-or-Q. Shared words do not preserve
  the logical connective.

## Scope and limitations

These twelve bits are one deliberately coarse structural projection, not a full
meaning representation or another canonical graph. Different families can share
vectors (including all-zero vectors); equal vectors cannot justify identity,
Claim merging or full semantic equivalence. Graph classification results remain
proposals with provenance. Compare declared perturbation pairs/groups instead of
claiming that every unrelated state must be separated.

Authored examples explicitly label P/Q or target T where needed for unambiguous
role/scope judgment. They are short and clean, not evidence of robustness on noisy
natural conversations. Some foil wording must change to express a different
relation; their vocabulary is strongly shared, not necessarily an identical bag
of words. Paraphrases can change incidental domain details while preserving the
specified structural projection; this does not assert full propositional identity.

Binary labels deliberately have no unknown state because each authored question
has a declared narrow reading. Unclear real inputs require an abstention-capable
protocol; this pilot cannot measure that behavior. Twelve separate questions may
be statistically and logically dependent. Translations and variants are correlated;
report family-level group results and do not treat 64 rows as independent samples.

Gold labels are author judgments, not independent adjudication. The fixture build
verified unique IDs, complete binary labels, invariant groups, foil differences,
family split integrity and byte hashes only; those are mechanical checks, not
model accuracy. Evaluation thresholds are intentionally not chosen from results.
