# Independent context scope and detail — 2026-09-30

This implements the first directly usable part of owner requirement R28 in
native `ContextRequest` and `ContextEngine`. The request controls now used by the
chat path can vary graph reach and item representation independently of the
token budget.

| Request field | Values | Default | Effect |
| --- | --- | --- | --- |
| `relation_hops` | Nonnegative native integer | `1` | Exploratory goal-band radius through existing claim subject/object links, in either direction. |
| `detail_resolution` | `null`, `label`, `summary`, `full`, `raw` | `null` | Override the representation of each selected item; null keeps the goal type's per-role policy. |
| `budget_tokens` | Existing token budget | Existing default | Limits which rendered items fit; neither defines scope nor selects a detail level. |

Example: keep the same graph radius while requesting more supporting detail:

```json
{
  "text": "How does the component depend on its surrounding modules?",
  "targets": ["e_known_component"],
  "relation_hops": 2,
  "detail_resolution": "full",
  "budget_tokens": 4000
}
```

`relation_hops: 0` is valid: it disables exploratory goal-edge gathering while
retaining existing stable/project candidates and required-premise closure. The
radius starts at explicit targets and the existing project anchor. Visited
entities and claim IDs make cyclic graphs terminate. Only evidence classes
admitted by the goal type participate; an excluded claim is not an invisible
bridge to a different part of the graph. Empty seeds are ignored, fixing the
previous possibility that an empty target matched unrelated literal-valued
claims through their empty object ID.

Graph reach is geometric, not a claim of relevance, correctness, identity or
authority. Traversal does not merge entities or infer new relations. Existing
relevance, authority, freshness, confidence and diversity scoring remain in use.
The existing per-endpoint `ClaimQuery` limit of 10,000 rows and existing handling
of failed store queries remain; this increment does not guarantee complete
coverage at high-degree nodes or report a complete traversal audit. Paging and
explicit truncation/error evidence remain follow-up work.

Changing detail leaves exploratory candidate gathering unchanged. With ample
budget the same references remain and their text changes. Under a constrained
budget, fuller representations can fit fewer items. Premises remain mandatory
dependencies: if a needed premise cannot fit, the existing `missing_premises`
and rendered `INCOMPLETE` marker stay visible. Explicit detail also applies to
premises pulled from outside the exploratory radius.

`raw` uses the existing per-kind raw view: supporting quotes for claims or a
serialized knowledge record for principles and decisions. It does not mean the
entire original source file. Existing fallbacks apply where the stored record
has no raw view. No summarization, embedding or classification model is called
by these controls; selection and preview remain offline even when credentials
and a semantic model are configured.

Default request serialization, selected-item representations and context IDs
remain unchanged. Nondefault controls appear in `goal.params.context_controls`,
and graph-derived items record their reach in `factors.relation_hops`. Nondefault
context IDs incorporate the controls alongside the existing base context ID;
the goal identity remains the same. This distinction does not redesign the
older identity scheme for unrelated selection inputs.

The dedicated `unit.test_context_controls` test executable covers nine cases:
request round trips and invalid inputs; same scope with different detail;
wider scope with the same detail; incoming/cyclic edges and excluded bridges;
zero-hop dependency preservation; very large radius termination; constrained
budget premise markers; trace/context identity; offline calls and empty seeds.
Native build and execution evidence is recorded by the session's verification
owner alongside the complete candidate test results.

Remaining R28 work is plan-aware selection with distinct scopes/details for
individual theses, explicit counter-evidence coverage and gaps. The basic
request-level scope/detail controls should not be reimplemented as that next
step. Semantic candidate generation and ranking evaluation are separate work.
