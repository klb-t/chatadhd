# Structure methods experiment

This is a read-only research harness under `loom/tools`, not a new knowledge
store or production reasoning engine. It compares **projections of existing
claims** and reports candidate consequences of supplied logical annotations.
It has no dependency on a benchmark corpus, model provider, numerical library,
database or network service. The independent evaluation owns its own corpus.

The experiment separates three questions:

1. Does a reasoning structure recur, possibly under different names or topics?
2. Is a proposed step valid in an explicitly declared logic?
3. Are its premises, interpretation and conditions supported in this context?

Graph resemblance answers only the first. The bounded proof search partially
answers the second, **conditional on its input**. Neither establishes the third.

## Model mapping

The authoritative meanings remain `docs/architecture/LOOM_CONCEPTUAL_MODEL.md`.
Source text belongs to immutable Observations with Locators. An explicit source
assertion is an assessed Claim, even if its assertion is incorrect. Formula
annotations are proposed interpretations attached to Claim IDs. Their extractor,
version, source spans and alternative interpretations must be retained by the
caller. A learned principle/template is a candidate Principle/Paradigm; a
cross-domain correspondence is a candidate Morphism; an inference method is an
Operator. Nothing here creates a fifteenth universal role or new evidence class.

The graph's `role` is always one of the fourteen universal roles (or unknown
empty string). Argument roles belong in `qualifiers.argument_role`, not `role`.
Formula vertices are ephemeral projection vertices. Their `claim_ids` refer to
the existing knowledge objects; vertices are not additional claims or facts.

For production adoption, the smallest proposal is a documented logical
annotation in `Claim.qualifiers.extra`, or a separately assessed interpretation
Claim if the annotation itself is uncertain. This experiment does **not** write
either. A stored inferred Claim must satisfy all current Assessment invariants.
The exact `proof_replay` Expected-Property predicate would require a binding
model addition and core verifier before persistence. Consequently every current
proof candidate has `persistable_claim: false`, `confidence: null`, and
`expected_property_proposal.implemented_in_core: false`; this is a report schema,
not an `Assessment` JSON object.

## Inputs and APIs

`structure_methods.py` exposes:

- `compare(left, right, rounds=2, budget=10000)`: four baseline scores and a
  bounded alignment report, with `validity: not_assessed`.
- `formula_graph(logic, abstraction='structural')`: logical annotations to a
  directed incidence graph. `semantic` preserves symbols; `structural` permits
  consistent symbol renaming. Logical connectives, bindings, repeated symbols,
  argument position, quantifiers, negation, causal/implication distinction and
  modality remain visible.
- `infer(logic, max_rounds=3, max_candidates=128)`: conditional proof candidates,
  blocked inputs, provenance and explicit search limits.
- `coverage_report(graph)` and `pattern_profile(graph, occurrences)`: missing
  operation coverage, recurrence counts and retained-detail counts.

A comparison record is `{text, structure:{nodes,edges}}` or `{text,logic}`. A
node is `{id,kind,role,qualifiers,claim_ids}`. An edge is
`{source,target,predicate,qualifiers,claim_ids}`. IDs, array order and provenance
IDs are excluded from structural similarity; kind, role, relation direction and
qualifiers are preserved. A graph without annotations returns unrepresented
coverage, not an invented extraction.

A logic entry is `{claim_id,formula,assessment,qualifiers}`. `assessment` uses
the existing model's field spellings. Input premise eligibility is restricted to
active `observed`, `derived`, or `user` entries; observed entries need support,
derived entries need an operator, and transfer derivations never chain. This
prototype deliberately does not accept existing `inferred` premises. The caller
is responsible for full Assessment validation and byte-level source grounding.
Unknown semantic qualifiers, non-active claims, defeasible rules, exceptions,
existentials, modality, disjunction and causation cannot be silently treated as
strict deductive premises.

Formula schemas (all keys required, unknown keys rejected):

| Operation | Fields besides `op` | Trusted inference behavior |
|---|---|---|
| `atom` | `predicate`, string-array `args` | Exact proposition/term matching |
| `not` | `arg` formula | Explicit negation; no closed-world assumption |
| `implies` | `left`, `right` formulas | MP and MT |
| `forall` | `var`, `body` formula | Instantiate existing constants with capture avoidance |
| `and` | formula-array `args` | Conjunction elimination |
| `or` | formula-array `args` | Structure only |
| `exists` | `var`, `body` formula | Structure only |
| `modal` | `mode`, `arg` formula | Structure only |
| `causes` | `left`, `right` formulas | Structure only, never strict implication |

Variables are bound by `forall`/`exists`; other term strings are constants.
Predicate symbols are never renamed during inference. Structure comparison can
rename them consistently; this detects a shared form, not an equivalence of
meanings. `compare` reports both structural and semantic alignment for logic
records. A single `R(a,b)` and `R(b,a)` have the same form under renaming, while
semantic alignment distinguishes the names. Shared additional constraints can
make the role reversal structurally distinguishable too.

Registry `operations.json` declares families, inputs, outputs, preconditions and
execution coverage. This is an extensible inventory, not an exhaustive theory
of thought. Generic graph representation can preserve additional atoms and
typed relations; dedicated AST parsing/execution coverage is narrower. Unknown
operations remain explicit. Data cannot add executable inference semantics:
new trusted operators require reviewed code and tests.

## Run

```sh
python loom/tools/structure/structure_methods.py registry
python loom/tools/structure/structure_methods.py compare /tmp/pair.json
python loom/tools/structure/structure_methods.py infer /tmp/premises.json
python -m unittest discover -s loom/tools/structure -p 'test_structure_methods.py' -v
```

CLI outputs JSON to stdout; it never modifies the input, core DB, claims, pack
or production selection policy. `test_structure_methods.py` contains author-owned
mechanism checks. These establish algorithm behavior, not independent semantic
quality or recall from natural language.

## What is not implemented

There is no complete natural-language understanding, learned canonical ontology,
calibrated probability, production C++ adapter, persisted deduction, automatic
analogy transfer, causal identification, default logic, global theorem prover,
automatic rule/template mining, learned geometric cone or persistent-homology
method. Broader operations are graph-representable and explicitly nonexecutable.
The natural-language extraction experiment, when used, must report its own
coverage/errors separately from scores on externally supplied logical gold.

Full labeled isomorphism is an intentionally bounded research control, not the
production anchored partial-homomorphism matcher. Limits are reported as unknown.
1-WL is not an isomorphism or logical-equivalence proof. Empty vectors score zero
with separate coverage. Raw scores are not probabilities. Apparent proof absence
means only unsupported by this bounded subset, never disproven or false.
