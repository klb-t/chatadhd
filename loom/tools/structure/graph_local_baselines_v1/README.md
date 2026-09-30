# DEV-only local graph retrieval baseline arm

Read `REPORT.md` for evidence-ranking metrics, per-class naive diagnostics and
matched-budget counterexamples. Validation inputs/gold remain sealed. This arm
writes no existing adapter/source files and calls no provider API.

Frozen primary files: `PROTOCOL.md`,`policy.json`,`freeze_before_scores.json`,
`prepared_inputs.json`,`freeze_before_gold.json`. Raw `first_scores.json` retains
`prediction:null`and`source_judgment_available:false`; labels enter only the
postscore evaluation. `embedding/`contains independently hash-bound cached-model
protocol, raw vectors, exactstring/tokenization index and query/candidate cosines.
No model weights or runtime dependency copies are stored in this new directory.

`first_results.json`and`naive_predictions.json`preserve all fixed threshold
results, per-class/family/language denominators and gold-bound evidence targets.
The naive recipes are diagnostic controls, not semantic judgments. The secondary
set-union protocol/results remain distinct and development-inspired, with every
candidate count and matched context-budget control.

Reproduction uses unchangedDEVfixture files and the prior pinned local encoder
cache. These stages refuse overwriting their first outputs; run in a clean copy
of this directory if reproducing them:

```sh
python3 loom/tools/structure/graph_local_baselines_v1/local_baselines.py prepare
python3 loom/tools/structure/graph_local_baselines_v1/embed_queries.py
python3 loom/tools/structure/graph_local_baselines_v1/local_baselines.py score
python3 loom/tools/structure/graph_local_baselines_v1/evaluate_dev.py
python3 loom/tools/structure/graph_local_baselines_v1/secondary_set_union.py
python3 -m unittest discover -s loom/tools/structure/graph_local_baselines_v1 -p 'test_*.py' -v
python3 -m unittest discover -s loom/tools/structure -p 'test_graph_panel_live.py' -v
```

Do not run fixture-wide integrity tests that inspect sealed validation. OnlyDEV
readers in the unchanged adapter are used here. Final release/integration belongs
to root; no validation can be opened without explicit release after method and
scorer freeze.
