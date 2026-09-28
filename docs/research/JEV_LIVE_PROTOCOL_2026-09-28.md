# Jev structural classification pilot

The owner clarified that real Jev experiments are the main purpose of supplying
OpenRouter access. Synonyms and paraphrases should have little effect on the
extracted structure. Lexical and regex checks are secondary omission detectors.

## Registered experiment

`jev-structure-v1`: 64 authored texts, 12 fixed independent Noul questions each,
768 binary decisions. Eight families, Polish and English, four variants:
base, synonym/paraphrase, different domain with the same structure, and a foil
using similar vocabulary but a changed relationship. Whole families are divided
into 32 development and 32 validation texts. Questions and thresholds are frozen
before running either split; no tuning or gold labels enter inference.

The twelve questions cover a limited projection of implication direction,
quantification, generalization, negation scope, support/counterexample,
analogy/identity, causation and conjunction/disjunction. This is **not** a unique
encoding of meaning. Equal answer vectors do not imply equal thoughts or justify
merging graph nodes. The README records deliberate collisions and label limits.

Access uses the existing dedicated OpenRouter key. Request endpoint:
`POST https://openrouter.ai/api/alpha/decisions`; model `typesafe/jev-1.13`,
provider pinned to `typesafe`, fallbacks disabled. Record the actual dated model
returned by the endpoint. Fresh public catalog pricing is checked against
USD 0.042 per million input tokens and zero output cost.

Every call reserves USD 0.001; the entire run reserves USD 0.064, with a separate
USD 0.10 batch ceiling and the existing USD 2 nonresetting key limit. The native
continuation reserves USD 0.24142525; combined with the prior completed charges
and uncertain request's reservation, the currently scheduled experiments reserve
about USD 0.315 across both jobs. This is not a new USD 2 authorization per job.
The fixed jobs may run concurrently; each has a finite request count, exact
provider/price restrictions, durable first-attempt ledger and no paid retries.

Jev's documented response usage contains cost and token counts, but does not
promise the chat endpoint's `is_byok` field. Do not invent that requirement.
Generation metadata is an optional read-only billing cross-check; an unavailable
lookup does not invalidate a documented Decisions response. Actual detected BYOK
or cost over the reservation stops execution. No further owner setup is needed.

## Measurement and interpretation

Retain all first responses, request/response hashes, versions, costs, tokens and
latencies. Missing or failed queries stay in the planned denominator. Fixed
diagnostic threshold is 0.5; selective reporting uses probability <=0.2 or >=0.8.
These are experimental cutoffs, not calibrated production trust thresholds.

Report positive-label precision/recall/F1, Brier score and coverage alongside
accuracy, separately by language, family, variant, question and split. Compare
accuracy to a constant-negative baseline because labels are imbalanced. Measure
probability shifts and decision agreement for paraphrases/domain changes, and
sensitivity to the foil's actual changed bits. An always-negative classifier can
look invariant, so agreement must be read with correctness and foil sensitivity.

The output proposes classifications of source-supported structures. It must not
create canonical truth, merge unrelated nodes, choose a single topic when several
apply, or override deterministic graph and source validation. Jev does not emit
free-form structures or explanations; the experiment evaluates supplied structural
hypotheses, not unrestricted discovery of every possible thought form.

After results, document useful and failed uses, PL/EN differences, error modes,
abstention/escalation policy and limits of generalization. The small authored
corpus is a diagnostic, not real-archive or population validation.

## Primary documentation

- https://openrouter.ai/docs/guides/community/jev
- https://openrouter.ai/docs/guides/community/jev-tutorial
- https://openrouter.ai/docs/api/api-reference/alphadecisions/submit-a-decisions-request
- https://openrouter.ai/docs/api/api-reference/generations/get-generation
- https://openrouter.ai/api/v1/models/typesafe/jev-1.13/endpoints
