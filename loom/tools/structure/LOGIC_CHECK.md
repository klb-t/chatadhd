# Bounded propositional checking of candidate occurrence graphs

This offline experiment asks whether a caller-selected conclusion follows from
caller-selected premises **under an explicit interpretation of the supplied
graph**. It neither extracts graphs from text nor verifies that their meaning is
faithful to the source. It writes no canonical data, promotes no candidate and
makes no model/provider calls. It has no solver dependency.

The motivating source audit is
`../../../docs/research/GEMINI_LOGIC_AUDIT_2026-09-28.md`.

## API

```python
from logic_check import check_bundle
report = check_bundle(bundle, source_packet, premise_roots, conclusion_root,
                      limits=None)
```

The first two arguments use the unchanged public candidate graph contract.
`validate_bundle` runs before any interpretation. Premises are an explicit list
of distinct declared expression-root handles; the list may be empty. The
conclusion is one declared expression-root handle. Other declared roots are
reported as `ignored_roots`, never silently asserted. A conclusion also selected
as a premise is allowed and identified by `conclusion_is_premise`.

All supplied structure must fit one asserted assertion scope. Quotation,
hypothesis, unknown context, quantifiers, scope hierarchies, negative/unknown
structural polarity, `denotes`, binding relations and references to old Claims
as Assessment premises are unsupported. These are not discarded or interpreted
as ordinary assertions. Unsupported structure anywhere in the bundle refuses
the query, even when it is outside the selected root closure. Full source and
coverage metadata remain available; unknown source coverage is not a proof
failure and is not treated as complete semantic extraction.

Supported operations are:

| Operation | Explicit interpretation |
| --- | --- |
| `predicate_application` | Opaque Boolean atom identified only by the expression occurrence handle |
| `conditional` | Classical material implication, not causality, temporal ordering or a default rule |
| `negation` | Boolean complement of its body |
| `conjunction` | All member expressions true |

Identical text, labels, predicate symbols or arguments do **not** merge two
distinct application occurrences. Sharing the same application handle does
establish shared atomic identity in this experiment. This conservative choice
will leave some natural-language consequences underdetermined. An explicit
future identity projection needs its own contract and evidence; it must not be
introduced by changing these results silently.

## Results and evidence

| `status` | Meaning |
| --- | --- |
| `rejected` | Invalid source/bundle, malformed query, invalid limits or input preflight failure |
| `unsupported` | Structurally valid input contains semantics outside the supported fragment |
| `limit` | Exact search would exceed the configured bound; no partial search is represented as a proof |
| `inconsistent_premises` | No assignment satisfies all selected premises; no vacuous conclusion is advertised |
| `entailed` | Premises are satisfiable and every satisfying assignment makes the conclusion true |
| `contradicted` | Premises are satisfiable and every satisfying assignment makes the conclusion false |
| `undetermined` | Satisfying premise assignments exist with either conclusion value |

The report includes `validation`, `packet_hash`, original `retained_input`,
`assumptions`, `limits`, `atoms`, `expression_sources`, `ignored_roots`,
`assignments_checked`, `required_assignments`, `premise_models`, and
`premise_consistent`. Selected expressions carry exact source support with copied
locators and Observation text hashes. The original packet and full bundle retain
the support of terms and unselected expressions as well.

`witnesses` has `premises`, `conclusion_true` and `conclusion_false` entries.
Each is `null` or a map from atomic occurrence handles to Boolean values. Every
returned witness satisfies the premises. A `conclusion_false` witness is an
explicit counterexample to entailment; a `conclusion_true` witness is a
counterexample to contradiction. No witness is an assertion about the world.
The enumeration is exhaustive before reporting any supported logical decision.

All reports keep `no_persistence` and `no_promotion` true and
`source_interpretation_validated` / `source_truth_validated` false. `entailed`
therefore means only formal consequence of the selected interpreted expressions.
Source offsets passing validation and a successful proof are two different
checks; neither establishes correct natural-language extraction.

## Bounds

`limits` can only lower `max_atoms` (default/hard maximum 12) and
`max_assignments` (default/hard maximum 4,096). Values must be positive integers;
booleans, unknown names and larger values are rejected. Only atoms reachable
from the explicit query are enumerated. Input is preflighted before recursive
copying/validation: depth 128, 100,000 visited JSON values, at most 4 MiB of
UTF-8 string contents and integers of at most 4,096 bits. Candidate validation
then applies its own stricter packet/bundle/resource limits.

Ordinary rejected JSON-shaped inputs are retained. Hard preflight failures use
`retained_input: null` and `retention.status: not_copied`, with ownership left to
the caller. The stdin CLI reads at most 4 MiB. CLI wrapper parse/shape rejection
also does not copy unparsed input into its output.

## Reproduce

From the repository root:

```bash
python -m unittest discover -s loom/tools/structure -p 'test_logic_check.py' -v
python loom/tools/structure/test_logic_check.py --example > /tmp/loom-logic-query.json
python loom/tools/structure/logic_check.py < /tmp/loom-logic-query.json > /tmp/loom-logic-report.json
python -c 'import json; r=json.load(open("/tmp/loom-logic-report.json")); print(r["status"], r["assignments_checked"], r["witnesses"])'
```

The hand-authored example gives `entailed` after four assignments. It is an
example of modus ponens, not a claim that an extractor analyzed the Polish
sentence. Thirteen author test methods cover manual inference/counterexample
cases, contradiction handling, identity, qualifiers, unsupported context,
external references, source retention, bounds, malformed input and CLI behavior.
One deliberately wrong but exactly quoted source interpretation still passes
the formal checker, demonstrating why the source-fidelity flags remain false.
Independent evaluation is recorded separately; author checks do not establish
model quality, broad language coverage or suitability for canonical promotion.
