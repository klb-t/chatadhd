# Context scorer independent adoption review — 2026-09-30

**Executable and frozen for the 48-request development context arm.** This
reviews two files recovered as **untracked candidates from the copied previous
workspace**, not an already integrated remote increment. No actual context
responses were read to make this decision. No key file, API call, graph write,
validation-label parsing or edit to the active pilot/runner was performed.

## Scope and decision

Read the complete `jev_context_score.py`, `test_jev_context_score.py`, current
`JEV_CONTEXT_PROTOCOL_2026-09-28.md`, context-pilot source and underlying fixture
protocol/README. The projection is 48 independent causal prefixes from eight
development conversations/four bilingual families, with **84 supplied-topic
membership bits and 44 supplied eligible-Claim selection bits**. This is a
conditional topic/context selector; it does **not** measure unconstrained topic
discovery, free graph/edge extraction, archive retrieval recall or a global
model reliability score. Candidate relevance is separate from asserting truth
or historical availability. Bilingual pairs and repeated prefixes are correlated.

**Keep/adopt the scorer unchanged.** No scoring defect was found in the
reviewed path. Initial ten mechanism tests passed. Add three independent
counterexamples before freeze: all 48 missing outputs preserve the full 84/44
bit and 48-query denominators; changing request-local question positions while
preserving ID/probability correspondence preserves metrics; an all-negative
valid set has exact-set success while precision/recall/F1 remain null when
positive opportunities are absent. These are mechanism checks with fabricated
outputs, never a model-quality experiment.

## Verified contract

- `development_gold` matches only each JSONL row's leading opaque case ID,
  skips non-development rows **before** `safe.parse_json`, and requires exact
  development inventory. Validation label bodies are not decoded or printed.
  The shared raw file is hashed against its existing pinned hash; this allowed
  integrity operation is distinct from interpreting held-back labels.
- `verified_bundle` checks the 48-item inventory and canonical artifact hashes,
  current prompt/code hashes, manifest/request identity and body hashes. It
  rebuilds the input/map from each retained causal export; `q01` is mapped to
  its particular topic/Claim ID, never aggregated as a stable phenomenon.
- `score` verifies fixture hashes and the ledger/first-response hashes before
  semantic scoring. Malformed completed responses fail the whole query;
  tampering aborts. Missing/failed attempts remain planned units rather than
  fabricated all-false predictions. No repair or retry path exists here.
- Membership and Claim TP/FP/FN, precision/recall/F1, valid-output coverage,
  all-query and valid-output accuracy are separate. Unavailable positives are
  FN; unavailable negatives do not become TN. Null denominators remain null.
  Joint/full exact-set accuracy retains all 48 units; empty Claim sets require
  valid responses. The candidate-bearing Claim subset is also reported.
- Threshold `p >= .5`, exploratory selective thresholds `.2/.8`, negative
  prevalence, always-false baseline, per-family/per-language breakdowns and
  individual errors are predeclared. Scores/Brier results are not promoted to
  calibrated production confidence. Cost output is ledger-reported cost;
  complete billing/uncertain-attempt accounting remains in the frozen runner
  ledger and must be included when reporting actual results.

No development/validation recipe tuning, quality-threshold reduction or
validation activation occurred. The fixture's authored development judgments
are not an independent unseen benchmark. Freeze this scorer/model/recipe before
any subsequent separately reviewed validation arm.

## Evidence and freeze

Pre-adoption source/test snapshots, hashes and first ten-test output are in
`context_scorer_first_snapshot/`. The source scorer remained byte-identical;
only its untracked test file gained the three counterexamples. Final command:

```sh
env TMPDIR=/var/tmp python -B -m unittest discover \
  -s loom/tools/structure -p 'test_jev_context_score.py' -v
```

**13/13 passed**; private `/var/tmp` was writable through approved local test
execution, so the existing private-artifact path guard was not weakened.
`context_scorer_test_output.txt` preserves the final output.

Freeze time: **2026-09-30T02:06:25.849676+00:00**.

| File | SHA-256 |
|---|---|
| `loom/tools/structure/jev_context_score.py` | `41975d03937a9f15027eadab3f3ecb32a091a1cb4bdf3b52741963466c41c714` |
| `loom/tools/structure/test_jev_context_score.py` | `e9da4a4a5eb660cc2c07dd5fa9ba5dc5d564bb960e9f36020f84f428883c93a9` |

Machine-readable status is `context_scorer_freeze.json`. Parent may now prepare
and run the unchanged 48 actual requests once under its separately verified
authorization/budget. After response capture, run the frozen scorer with the
actual manifest/run/prepared directories. Retain all first responses and error
units, and interpret results only for this supplied-topic/supplied-candidate,
causal-prefix, development-family and exact recipe/model/provider profile.
