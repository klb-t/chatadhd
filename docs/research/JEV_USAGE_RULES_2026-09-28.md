# Jev usage rules after the first live pilot

Status: proposed integration policy grounded in the measured diagnostic in
`JEV_RESULTS_2026-09-28.md`. The live adapter and scorer exist; the full native
pipeline below is not yet integrated or validated on real conversation archives.

## Local follow-up evidence (2026-09-28)

The separately frozen direct-pair task completed: 47/48 first judgments matched
its labels, including all 16 paraphrases and all 16 structural foils. Positive
precision was 31/31 and recall31/32. The sole mismatch was an analogy whose coarse
operation-level pattern agrees while finer relation topology differs. The
predeclared 0.2/0.8 review interval retained46/48 with no observed retained error;
this small reused corpus does not establish calibration or production accuracy.
See `JEV_PAIRS_RESULTS_2026-09-28.md` for cost, language breakdown, first artifacts
and the original final-key-check failure plus separate successful reconciliation.

Consequently, comparison proposals should name their abstraction/projection and
retain more than one comparison dimension where useful. A coarse analogy score
must not silently stand for fine role/topology equivalence. Keep the original
rubric and threshold with each assessment so a different projection can be
re-evaluated without relabelling the earlier evidence. A same-task chat-model
comparison and a supplied-topic/context selection pilot are separate experiments;
the pair result alone does not choose an exclusive production classifier.

## Preserve the owner's actual target

Represent thought and argument structure in the existing authoritative graph.
Compare disposable projections of its subgraphs, not a second canonical store.
The projection should ignore irrelevant name/synonym/domain changes while retaining
the relationships and constraints relevant at the chosen abstraction level.
Lexical and regex signals can flag missed source material or suggest retrieval
candidates. They must not define structural equivalence or veto a cross-domain
pattern match.

An abstraction must declare what it discards. Preserve argument roles, direction,
binder ownership, repeated occurrences, scope, negation, modality and relevant
quantifier distinctions. Consistent alpha-renaming is useful; unrestricted
renaming can make an isolated R(a,b) and R(b,a) isomorphic. A reversal check needs
anchored roles or surrounding constraints. Similar structure is not entity identity.

## Suggested processing flow

1. Retain source observations and retrieve relevant existing graph context with
   its chronology, provenance and alternative interpretations.
2. Propose atomic operations, roles, scope and candidate relations. This step may
   use the separately configured inexpensive semantic model. Jev judges supplied
   hypotheses; it does not itself emit an unrestricted graph or reasoning trace.
3. Ask Jev narrow questions about these hypotheses, allowing multiple simultaneous
   patterns/topics. Keep source-expressed relations distinct from inferred
   generalizations or plausible mechanisms. Both can be valuable with different
   epistemic status.
4. Construct graph mechanics deterministically where possible: handles, edge
   fields, source coordinates and schema constraints. Validate chronology,
   grounded roles, graph admissibility and source references in code.
5. Attach results as provenance-bearing Assessments or reversible proposals.
   Compare scoped graph projections across subjects; propose analogies, recurring
   patterns, unanswered implications and source revisits. Do not merge entities
   or transfer truth merely because the shape matches.
6. Run a secondary lexical/source coverage check to seek omissions. An omission
   should reopen an interpretation, not let a keyword rule silently replace it.

This flow is a direction for the next implementation experiment. The original
native extraction pilot shows why graph serialization/coordinate burdens should
be separated from semantic judgments, but has not yet validated this replacement.

## Pick the decision primitive by its meaning

| Primitive | Appropriate use | Avoid |
| --- | --- | --- |
| Noul | Independent relation properties, relevance to several topics, evidence sufficiency | Forcing independent scores to sum to one |
| Choice | One genuinely exclusive interpretation among supplied candidates, with unresolved/out-of-set option | One winning project when a message belongs to several |
| Score | Ranking relevance or investigation priority under an explicit ordered rubric | Treating rubric position or concentrated probability as truth |

Only Noul has been measured in this increment. Choice/Score rules follow the
documented interface semantics and still need comparable live evaluation.

## Applicability and uncertainty

Missing roles or insufficient evidence are not the same as a false relation.
The first fixture's absent-P/Q rule is a narrow test convention, not a production
rule saying ordinary prose lacks implication. Either provide grounded candidate
bindings or explicitly evaluate an inferred binding as its own proposal. Do not
reject abstractions solely because their variables are not spelled out in text.

Keep a review interval and version thresholds by task, language and model version.
The pilot's 0.5 threshold and 0.2/0.8 selective interval are experimental choices.
Even p>=0.8 produced five false positive labels. Question concentration is not
empirical correctness. Candidate retrieval coverage must be measured separately:
Jev cannot recover an omitted candidate merely by rejecting supplied alternatives.

## Reuse and costs

Cache by exact selected packet/time cut, candidate IDs and role bindings,
abstraction/schema version, question/rubric, model/provider version and settings.
Keep raw scores separate from threshold decisions so policy changes can be replayed
without another paid request. Never cache a missing or uncertain answer as false.
Group independent questions sharing the same state in one request; they cannot
condition on one another's answers. A dependent question needs another explicit
stage with its preceding result and provenance.

Keep finite request and monetary budgets, first responses and unknown-attempt
reservations. No paid retry for semantic disagreement. A changed method or a
repair arm is a new, separately measured experiment. Optional billing metadata
unavailable on the Decisions route must not become a new account-setup blocker.

## Required next evidence

Compare direct structure-pair judgments and bound-role judgments on fresh natural
examples, including contradictions, multiple topics and late returns. Add role
renaming, scope foils, distractors, quoted instructions and question/option-order
perturbations. Compare other models on the **same classification task**, not their
ability to serialize a larger graph. Report positive precision/recall, abstention,
complete-structure accuracy, candidate coverage, cost and latency separately.

Keep validation families unseen until the method is frozen. Reusing inspected
validation examples for a new question design is exploratory; obtain a new holdout
before claiming improved generalization. Source annotations and binding aids are
candidate conditioning, not evidence that free-form structure discovery is solved.
