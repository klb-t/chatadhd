# Philosophy watch, snapshot 01

2026-09-30. Local research operations were examined after the first paid batches;
this audit made **zero model calls** and did not open any sealed validation or
holdout. The immutable first check results are in `FIRST_MECHANISM_RESULTS.json`:
**14/14 mechanism invariants pass; 3/3 diagnostic counterexamples reproduce**.
These counts measure mechanisms, not model accuracy or population prevalence.

## The current owner instruction changes the old default

The complete updated remote `AGENTS.md` and `HANDOFF_SOL_2026-09-29.md` were read
from the parent's retrieved `research-recovery/remote-updates/` files. The current
AGENTS heading says model, scope, reasoning, auto-acceptance and what is sent are
configurable; defaults are presets; an expected usage increase of at least 10×
requires the owner's confirmation. HANDOFF §3 says no paid calls on synthetic
data; §3.7 says no paid pilot and explicitly corrects the old frontier-only-on-a-
selected-part and never-auto-promote constraints. Live research waits for owner
archives. The older unchanged §4 auto-promotion sentence is inconsistent with
this explicit correction. It must not become a global ban in application code.

The distinction that survives is epistemic: **acceptance is a decision; origin
and evidence are records of how a claim was obtained**. A user may choose to
automatically apply a frontier model's whole graph diff. That policy does not
rewrite the original sources, make the model response a user statement, prove
semantic source support, or make an inferred consequence a directly observed
world fact. The application can retain accepted model-origin claims and explain
the selected acceptance policy. There is no invented obligatory review step.

## Counterexamples and exact implications

1. **Exact source binding is not semantic grounding.** The current compiler
   accepts a supplied reverse B→A with exact quote `Żółć 🧪` from a turn that
   states only A→B. Direct lookup returns supported **relative to the supplied
   typed record**, while retaining semantic verification false and world content
   unverified. Another supplied candidate with an invented nonempty actor and
   the full source turn also compiles. No model generated these two deliberately
   wrong candidates. This is a boundary test, not a discovered paid-model error.
   Keep locators as locators; measure semantic grounding separately. If such a
   record is accepted automatically, retain its actual model/policy provenance
   and verification state instead of certifying entailment.
2. **Future event validation can affect a past formal query.** A positive A→B
   at T1 yields supported at cutoff T1. Appending a source-bound cross-speaker
   supersession at T2 makes `graph_formal_paths.solve_paths` raise
   `cross_speaker_supersession` even at cutoff T1: event validation happens before
   temporal eligibility filtering. The future event itself is not used as a
   premise. This diagnoses whole-graph validation dependence; it is not evidence
   of a model's hidden future-knowledge leakage. A causal prefix pipeline must
   compile/evaluate physically eligible graph records, or declare that invalid
   later records can invalidate the whole input. Do not alter frozen first
   formal-arm code or describe the retrospective arm as causal.
3. **Current-source projection preserves choices and history.** The named
   individual-source policy withholds a cross-speaker event in the derived view
   while retaining the entire original graph and the event in its audit. This
   fits optionality and one-source-of-truth requirements. It is not a universal
   authority rule: institutional delegation, source identity aliases and other
   legitimate correction scopes need distinct data policies, not silent reuse.

Other passing checks cover immutable source/candidate objects, UTF-8 locators,
physical prefix exclusion, absence→unknown, no reverse/contraposition inference,
opaque signed nodes, retained alternative formal paths, inferred/unverified
classification, rejected extrapolated premises, explicit incomplete search
bounds, conflict diagnostics and honest unsupported withdrawal ABI. No confidence
or world-truth metric is invented.

## Assessment of the observed research progress

- Stored first results now distinguish retrieval, supplied-edge judgment,
  assisted extraction, citation compilation, source-commitment projection and
  formal deduction. They should stay separate conditional instrument profiles.
- `95/96` after optional source projection is a downstream development result;
  it does not replace the original `0 TP / 9 FP / 6 FN` strict event metric.
- Jev packing's equal-body variation is an observed instrument property on four
  selected DEV queries. The largest A/B mean contrast is smaller than within-arm
  ranges. No causal packing effect, independence or calibration follows.
- Historical first API outcomes remain valuable evidence, but the newly read
  owner no-paid-synthetic/no-pilot instruction prevents extending them with new
  paid synthetic batches. Offline replay, mechanism experiments, method
  implementation and first-class frontier/agent configuration remain productive.
- Whole-conversation extraction followed by source-time cutoff is retrospective;
  source `known_at`, model response receipt time and knowledge-availability time
  must remain separately named. No temporal holdout quality was established.

## Watch scope

This directory is a research audit/proposal surface, not another canonical graph
store. First snapshots are append-only. A new snapshot is warranted when a
method, policy, graph exchange contract, benchmark population or acceptance
semantics changes materially. No repeated polling loop or automatic spending is
introduced. The parent retains integration and all paid execution authority.
