# Exploratory Jev structural pair comparison v1

**This is a post hoc exploratory follow-up using previously inspected material.**
It reuses the frozen `jev_structure_pilot_v1` sources after the first 64-state Jev
run was inspected. The inherited `validation` split is **not a new holdout** and
must never be reported as independent confirmation of a newly designed question.
No original source text, label, grouping or split is changed by this fixture.

There are 48 standalone pair queries: eight families × Polish/English × three
contrasts. Each compares the original base passage with its paraphrase,
cross-domain transfer or vocabulary-sharing structural foil. The single fixed
Noul question asks whether the passages share the declared operation-and-role
skeleton. It does not ask whether their words or complete propositions match.

Only `state` and `questions` are model-prompt content. `state.text` contains a JSON
string with exactly `left` and `right`, preserving the original source text.
The input envelope also has opaque `case_id` and `language` request metadata.
No family, split, contrast kind, source-case ID, answer or sibling pair is sent.
The same question and criteria are used unchanged for every pair.

Gold q01 is 1 for the 16 paraphrase and 16 domain-transfer pairs, and 0 for the
16 foil pairs, inherited from the original authored construction rather than
relabelled to fit an observed response. All gold `variant` values are deliberately
`base`, and each `base_case_id` equals its own `case_id`: each pair is one scoring
unit, not another within-arm invariance group. Analyze `contrast_kind` separately.
The existing runner can report binary classification metrics; no meaningful
invariance or foil-distance metric should be inferred from these self base IDs.

## Declared abstraction

Ignore domain, words, entity names, incidental attributes, finite sample count
and singular/collective domain descriptions of a role. Preserve explicit logical
quantification, negation scope, conjunction/disjunction, antecedent/consequent,
premise/conclusion, finite-to-universal induction versus an instance-restricted
conclusion, evidence for/against a target, analogy/identity and causation/association.
An ordinary-language passage does not need literal P/Q/T markers. Where these
local role markers occur in both sources, they anchor their corresponding roles;
renaming must not swap P and Q to erase the conditional reversal foil.

For the analogy family, the comparison is at the operation level: map relation
R(a,b) to R(c,d) across distinct systems. Controller/worker and tributary/river
fill relational roles with different domain meanings and internal topology; this
question intentionally abstracts those differences away. Under a finer graph
projection that preserves fan-in/fan-out or group cardinality, that transfer
could reasonably be judged different. This explicit choice must accompany results.

The question does not identify full logical equivalence classes. For example,
rewriting a negated conjunction into a disjunction is outside its declared
projection; the represented scope/operator distinction is retained. Nor does it
infer actual causation from a plausible event description or treat an unendorsed
quoted universal target as an asserted universal conclusion.

## Limitations and verification

Pair order is always original base on the left; orientation robustness has not
been measured. Reused sources, translations and the three contrasts within each
family are correlated. Aggregate by family and contrast as well as language;
48 calls are not 48 independent semantic situations. Cases are short authored
examples with clean role cues, not noisy conversations or unconstrained structure
discovery. Binary labels provide no genuine ambiguity/abstention opportunities.

The coarse skeleton may match while important facts or finer graph structure
differ. A positive result supports a comparison proposal only; it is not entity
identity, an automatic Claim merge, truth transfer or permission to modify the
canonical graph. Equal patterns cannot supply missing source evidence.

The fixture records exact hashes of unchanged source inputs/gold/manifest and
of its own inputs/gold/README. Mechanical checks verify 48 unique pairs, 32
positive and 16 negative labels, 16 examples per contrast, 24 per inherited
split, exact left/right byte-preserving source reuse and one shared question.
These checks are not model accuracy. The author made no model calls for this
fixture; the corpus is frozen before any inference using this new pair question.
