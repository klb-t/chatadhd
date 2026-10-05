# New-label-corpus-v1: source commitment rubric

This is newly authored, author-visible synthetic evaluation DATA. It contains
24 distinct fictional conversation families, 12 Polish and 12 English, with four
queries per family. It is not a blind sealed holdout, world-truth benchmark,
general adequacy assessment, user export or evidence about real people.

Authorship followed root's global selection/preset freeze commit
`7aed09b9df6b8d2fc0afc3a399f12016795ca687` (2026-10-05 12:36:53 UTC).
No earlier conversation families, gold labels, source-relative accuracy metrics
or model responses were read by the author. The exposure log records the
instruction/code/document metadata that was visible, including broader-than-
requested source-function display. Root sees the gold for peer review after the
selection freeze. Readiness is separate from the subsequent root DATA freeze.

## Target and labels

For each query, inspect only turns whose `known_at` is at or before `as_of`.
Evaluate the requested speaker's latest applicable explicit commitment to the
ordered `(source proposition, relation, target proposition)` at that view.
The node inventory identifies the complete propositions. `requires` uses the
direction “source requires target”; `prevents` uses “source prevents target”.
The propositions do not carry truth status merely because they are in inventory.

- `supported`: an active explicit positive assertion of the exact ordered
  relation is attributable to the requested speaker by the view time.
- `refuted`: the latest applicable commitment explicitly denies that relation
  or explicitly withdraws the speaker's positive assertion of it. This concerns
  commitment in the source, not falsity of the relation in the world.
- `unknown`: neither such active positive nor such active denial/positive
  withdrawal exists. Silence, mentioning both objects, questions and statements
  that the speaker takes no position do not themselves deny a relation.

A later explicit affirmation by the same attributed speaker replaces that
speaker's earlier negative commitment. Explicit withdrawal of a negative
commitment leaves `unknown` unless a distinct explicit positive commitment
remains. Withdrawal itself is never an affirmation. Unrelated later speech does
not reactivate a withdrawn negative. Another speaker cannot retract or supersede
the requested speaker's commitment by disagreeing with it.

An expressly attributed quotation counts for its quoted speaker. Reporting it
without endorsement does not count for the reporter. A later explicit adoption
as one's own position does count for that adopting speaker. None of these rules
establishes the quotation's real-world authenticity.

Reverse directions are separate relations. A withdrawal, denial or affirmation
of P→Q supplies no commitment to Q→P. Negation inside either operand belongs to
that proposition, and differs from denying the complete relation. A positive
statement about a negated operand remains a positive relation statement.

All cases use the supplied `explicit_source` scope. No branch scope is silently
invented: the frozen adapter constructs physical timestamp prefixes and does not
provide a branch-selection mechanism. Queries intentionally vary their temporal
views, attribution or direction within each family; their family IDs never imply
a label ordering. Families are independent compositions, not translated copies.
The author's choice to include multiple views, multiple queried speakers and both
directions in every family is DATA design for this corpus. It is neither an
engine requirement nor a universal quota. `corpus-design.json` records the
authored inventory and selected recipe variants; the producer uses dynamic
inventory counters rather than introducing fixed numeric execution ceilings.

## Source rationale and exact evidence

`inputs.json` contains source DATA and queries without labels or rationales.
`gold.json` separately contains `cases[{id, judgments:[...]}]` with one label,
language-matched rationale, exact source/turn reference and spans per query.
Gold evidence quotes are complete, unique turn substrings. `span` uses Unicode
code-point offsets and `utf8_span` uses byte offsets within that same turn.
`visible_prefix_turn_ids` makes the inspected view explicit, especially where
absence of an asserted reverse relation supports `unknown`. Referenced turns are
all inside the query's view; unavailable future text is never evidence.

Full turn quotes preserve explicit attribution and correction context. Evidence
for `unknown` identifies relevant reporting, direction, withdrawal or non-
commitment context; absence is assessed over the whole recorded visible prefix,
not inferred from the mere existence of a cited unrelated sentence.

## Frozen methods and replay boundary

The only evaluated methods are the already selected `j_active`
(`active_refute_v2`) and `j_directed` (`directed_refute_v3`). The producer calls
`w3_directed_commitment_v1.experiment.specs(cases, arm)` for these two arms only;
each yields exactly 96 distinct query rows. It checks role identity and temporal
prefixes and verifies that the only recipe delta is the frozen append to
`questions.q02.criteria.false`. It reads no gold and makes no model calls.

Run from the repository root:

```sh
python docs/research/model_research_2026-10-04/stage5/new-label-corpus-v1/replay_producer.py
```

The producer refuses an existing output with different bytes. Its audit hook
blocks sockets, process spawning, old research fixtures, historical result
folders, gold, ledgers, key paths and response files. No old `prepare_specs`,
`prepare_plan` or `load_fixture` entry point is called. The DATA size of 96 is not
an added runtime/request ceiling. Root owns paid manifests, pricing, budget/cost
accounting, collection and scoring. Root freezes the authored DATA after peer
review and before collection.
After collection begins, authored source/gold DATA must not be changed in response
to model responses.

## Limits on conclusions

Structural validation proves inventory, role, prefix, evidence and frozen-spec
consistency. It cannot prove label correctness, population representativeness or
statistical independence from model pretraining. Root peer review checks the
labels before collection. Any later performance claim must name this newly
authored synthetic population and use family-level denominators as well as query
denominators; no external or real-conversation adequacy claim follows.
