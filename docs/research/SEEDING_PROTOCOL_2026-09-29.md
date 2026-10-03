# T7 / R36 structural seeding: protocol frozen before outcomes

Protocol version `synthetic-lopo-v1`, recorded 2026-09-29. This is an offline
mechanism prototype, not a model-quality or temporal-prediction claim. Only
`loom/tests/fixtures/eval/synthetic_dev/ground_truth.json` is used. It is a
development oracle describing five fictional projects, already inspected for
schema design. No blind holdout, real export, external service or production
graph is read or changed.

## Question and hypotheses

Can partial graph mappings transfer a missing relation more usefully than
uninformed proposals? We expect shared role/operator labels to be transferable,
and exact project-specific feature labels to expose a vocabulary ceiling. A
failure on novel labels is retained, not repaired with target-specific rules.

Three independently reported tasks:

1. **Roles**: hide one observed universal-role edge from a target project. Inferable
   and absent-role annotations never enter the input or positive labels.
2. **Capabilities**: hide one project-to-applied-operator edge. These are
   *solution-class capability proxies*, not evidence of implemented code modules.
3. **Features/modules**: hide one `features_status` element, including its entire
   record. These are corpus feature proxies, including planned/lost/abandoned
   elements; no prediction means “currently implemented”.

Every eligible element produces one mask case. Add one no-removal negative
control per project and task. Only the hidden element is a positive in a mask
case; the negative control has none. Closed-world scoring treats other proposals
as false positives **for reconstruction**, not as proof that a plausible new
capability is false in reality. Raw feature prose and role quotes are never
target input, preventing duplicate textual descriptions from undoing masking.

## Split and leakage boundary

Leave one **entire project** out for each fold. Donor graphs, candidate vocabulary,
frequencies, operator solution text, and expected properties come exclusively
from the other four projects. The predictor receives a separately constructed
target view containing only project id/kind and visible labelled edges. No
target descriptions, aliases, decisions, feature events, source quotes, hidden
ids or target expected properties are passed. The masking harness retains labels
separately and scores only after prediction records have been written.

Global operator definitions are used only when their id occurs in a training
project; expected properties are justified by training decision evidence. They
are oracle annotations, not an automatically extracted graph. A global
definition might itself have been authored with whole-corpus knowledge: this
experiment cannot establish temporal independence. Each fold is held-out at
project level **within the development corpus**, not an independent blind corpus.

## Fixed methods and measurements

Policy is a JSON data file. No fitting or threshold search in this protocol.

- `partial_mapping`: align each donor project with the visible target project by
  common labelled outgoing relations. Score the mapping by a weighted Jaccard of
  those relations (roles 1, capabilities 2, principles 1, features 1); require at
  least one preserved edge. Transfer donor elements absent from the target.
  Rank by mapping score summed across supporting donor projects, then donor
  count, then canonical lexical label. No domain/kind match is required; its
  absence is explicitly unverified, preserving competing interpretations.
- `most_frequent`: rank unseen training elements by donor-project frequency,
  then canonical lexical label. Does not inspect the hidden label.
- `random`: uniformly permute the unseen training vocabulary using seeds
  0–31 and a stable case id. Save every seeded result; also compute the exact
  expectation under uniform ranking. A baseline may win and will be reported.

Primary budget: one candidate per case; diagnostic budget: three. Report true
positives / emitted candidates (precision) separately from true positives /
hidden elements (recall), false positives and false negatives, abstentions,
candidate-vocabulary coverage, per fold and task. Negative controls remain in
the precision denominator. Exact expectation for random is fractional and is
clearly labelled; recall on zero positives is unavailable, not 1. Confidence
scores are similarity scores, never calibrated truth probabilities.

## Epistemics and outputs

All candidates remain `extrapolated` / `candidate`; the canonical graph is never
mutated. Candidate records carry preserved premises/mapping, unverified and
expected properties, source locators, an explicit transfer operator, donor
projects, alternatives, assessment and counter-evidence (empty means “not
evaluated”, not “none exists”). In the current corrected output schema,
candidate `known_at = predicted_at` records its wall-clock creation in this run.
Source locator dates and `source_created_at_max` remain distinct from unknown
historical source know-times. The original pre-outcome snapshot's explicitly
labelled older donor-date semantics are preserved with its first results. Neither is a
predict-before-observe claim: the source corpus already exists and masking is
retrospective. Alternatives retain their own expected properties and provenance.

The CLI refuses to overwrite a run directory. `predictions.json.gz` is written
before `results.json.gz`; first outputs and all random repetitions are preserved.
Each output identifies fixture/protocol/policy/code hashes. Protocol changes
need a new version/run, with the first result left intact.

## Decision rule, fixed before execution

Keep the prototype mechanism only if masking, donor-only vocabulary,
determinism, denominators and hypothesis provenance pass independent tests.
Investigate empirical benefit if structural top-1 recall or precision fails to
exceed the frequency baseline; do not promote it as a useful generator merely
because tests pass. Exact feature cold-start failure requires an explicit future
operator/template generation mechanism and an independent evaluation corpus,
not target-labelled patches. No threshold changes or target-tuned second run are
planned in v1.

## Results (append only after the first run)

V1 first result: top-1 precision 7/61, recall 7/46; frequency 8/61 and 8/46.
Roles lose to frequency; capabilities have 0/10 top-1 recall despite vocabulary
coverage 8/10; exact feature labels have zero coverage. Full outputs and the
**pre-outcome** protocol/code/policy snapshot remain under
`loom/tools/seeding/results/synthetic_lopo_v1_first/`. Outcomes and the later
local-premise intervention are described in `loom/tools/seeding/README.md` and
`SEEDING_LOCAL_PREMISES_2026-09-30.md`. The latter also records a metadata
correction: source dates do not establish historical `known_at`. Neither run is
a temporal holdout or an independent quality measurement.
