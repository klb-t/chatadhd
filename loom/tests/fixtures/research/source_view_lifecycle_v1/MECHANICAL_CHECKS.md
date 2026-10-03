# Source-view lifecycle v1 — frozen fixture checks

Frozen before any method prediction: **2026-09-30T02:53:36.275453+00:00**.
This independent author did not read existing graph-panel response bodies,
old validation gold, credentials or indexes, and made zero API calls.

Fixture manifest SHA-256:
`c47a87f62dd7724d50243c40494d70ae1a20ccd278d3bf3e3b12d3f28ef4072b`.
Input SHA-256:
`63e056cd67cfb7e8257fbca291130e2942457b1bab324c46f3630593a53570e5`.
Gold SHA-256:
`ef42470e0a2ee4c1b559a6431d19929703522456517c24f1acb8477531b6f2c4`.
The manifest binds source, gold, policy, preregistration, author/test code and
the existing `graph_panel_live.py` dependency; it excludes itself and these
post-freeze integrity notes.

## Inventory and fixed labels

| Whole bilingual family | Case IDs | Labels in each PL/EN case, q1–q4 |
| --- | --- | --- |
| Reaffirmation after denial | `svlv1_dev_001` / `002` | supported, refuted, supported, unknown |
| Positive withdrawal without replacement | `003` / `004` | supported, refuted, refuted, unknown |
| Quoted withdrawal attribution | `005` / `006` | supported, refuted, unknown, unknown |
| Repeated positive history | `007` / `008` | supported, supported, supported, unknown |
| Other speaker reaffirmation | `009` / `010` | supported, refuted, supported, refuted |
| Withdrawn denial and silence | `011` / `012` | refuted, unknown, unknown, unknown |

There are **12 cases / 48 queries**, six PL and six EN cases, **18 supported /
14 refuted / 16 unknown** labels, 24 retained source assertions and 12 status
events. These are authored diagnostic DEV with supplied candidate nodes and
edges, not independently sampled natural conversations or a held-out dataset.
Each bilingual pair remains one correlated family.

`source_view.latest_active_commitment/1` explicitly maps withdrawal of a
positive commitment without replacement to `refuted`, and withdrawal of a
negative commitment without replacement to `unknown`. These are declared
source-commitment labels, not logical world-truth judgments. Quoted withdrawal
is attributed to Mira, while reporter Owen has no commitment. This annotation
does not verify that Mira performed that speech act in reality.

## Measured mechanical checks

Command, from repository root:

```sh
python -B -m unittest discover -s loom/tests/fixtures/research/source_view_lifecycle_v1 -p test_fixture.py -v
```

**15 tests passed**, 0 failures/errors, in 0.006 seconds. The first successful
run is retained in `first_mechanism_stderr.log` / `first_mechanism_stdout.log`.
It checks:

- exact 12/48 inventory, class denominators and whole bilingual families;
- unchanged input schema and absence of evaluator labels/families/traces;
- exact source/turn quotes and character/UTF-8 coordinates, including a
  nonzero multibyte offset and ambiguous-quote rejection;
- physical causal prefixes for all 48 queries, future-turn mutation isolation
  and deep-copy independence from original source;
- causal, attributed and directed gold traces, with no future backfill;
- all six lifecycle/attribution/history families and null withdrawal status;
- unchanged judgment scorer compatibility, missing/conflicting responses kept
  in all 48 denominators, and unchanged deterministic source/gold/policy hashes.

Gold replay is a **scorer wiring check**, not a measured method score. The fixed
always-unknown baseline mechanically has **16/48** correct labels; it misses
all 18 supported and all 14 refuted labels. Missing/invalid predictions have
0/48 available predictions and do not become unknown.

The first authoring invocation failed because standalone dynamic import did
not supply the existing adapter's sibling-module path. It generated no
source/gold/manifest and invoked no model. Its exact exception is retained in
`first_authoring_failure.log`. The author-only import context was corrected
before the first freeze; no existing adapter or production code changed.

## Integration boundary and next step

The input root is `loom.research.graph_methods_panel.inputs/1`, `split: dev`.
Unchanged `query_payload` works directly on every case/query and supplies only
the appropriate causal source prefix. Unchanged `score_judgments` accepts the
gold's `judgments` list and ignores research annotations. Gold uses its own
`loom.research.source_view_lifecycle.gold/1` schema.

The research `withdrawn` status with `superseded_by: null` is deliberately
**incompatible with the current extraction status-event ABI**, which requires
a replacement assertion. Do not send this gold to `compile_extraction`, or
claim that this panel measures extraction quality. Retained earlier assertions
are observations; active views remain reconstructable policy projections.

Root may now separately freeze method recipes and execution budgets, compare
unchanged v1 historical-refutation wording against v2 active-view wording on
the same 48 prefixes, and retain all first responses without paid retry. Report
per-class precision/recall and confusion, unavailable counts, family/language
breakdowns, temporal flips and attribution controls. No method outcomes have
been observed by the fixture author. No production behavior changed here.
