# Released generic local graph wrapper

Frozen release SHA-256:
`bc4d6afdca9cb99a2a660d1e3a90829654cf667ada0dd77de38b6e2eb588bdcd`.
All 15 dependencies are verified by every public packing, embedding and
evaluation entrypoint. No input/gold fixture is loaded by the wrapper.

The root owner supplies released cases, then calls:

```python
prepared = prepare_cases(cases, split='validation',
                         expected_release_sha256=release_sha)
embed_prepared(prepared, fresh_output_dir,
               expected_release_sha256=release_sha)
# Only after first vectors/scores and freeze_before_gold.json exist:
evaluate_cases(cases, golds, fresh_output_dir / 'first_scores.json',
               expected_release_sha256=release_sha)
```

`split='dev'` supports transport-equivalence replay. The fixture release and
opening of validation belong to the root workflow, not this wrapper. Formal
queries are recorded as excluded; they require a separate recipe. The same
DEV-frozen thresholds, text representations, model, maximum fusion and
evidence evaluator apply. Output directories and artifacts must be fresh.

Mechanism checks: 7/7. DEV transport replay reproduced all 96 query-prefix
objects, all 252 candidate score bindings, all first-score rows, all retrieval
group reports and all naive overall metric fields exactly. The vectors.npy
byte hash also matches the prior DEV artifact. Runtime inference took 1.605
seconds excluding model initialization, with no API call or download.

One initially overstrict report-equality assertion failed because this wrapper
does not attach the old naive report's `interpretation` string or its extra
naive subgroup reports. The numerical metrics and predictions matched; the
first outputs were preserved and frozen code was not modified in response.
`dev_transport_equivalence.json` records that discrepancy. Retrieval retains
all family/language/label groups. The old interpretation remains applicable:
naive threshold judgments are diagnostics, never semantic truth decisions.

The primary outputs keep `prediction: null` and
`source_judgment_available: false`; explicit-source retrieval conditions on a
supplied directed edge, rather than discovering an unseen graph. Low/missing
similarity cannot infer refutation. Validation was not accessed during this
wrapper's preparation, tests or DEV replay.
