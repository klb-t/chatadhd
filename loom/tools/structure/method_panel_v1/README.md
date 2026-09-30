# Frozen pair-method panel v1

Read `REPORT.md` for measured precision/recall, coverage, baselines, counterexamples
and decisions. `PROTOCOL.md`/`policy.json`/`freeze.json` precede first model-free
scores. `EMBEDDING_ADDENDUM_PROTOCOL.md` precedes evaluation of independently frozen
learned embedding scores. All 48 pair examples, including inherited validation,
are exploratory reused material. No provider/model calls occur in these scripts.

Reproduce model-free scores in a **new output directory** (existing evidence is
never overwritten):

```sh
python3 loom/tools/structure/method_panel_v1/methods_panel.py \
  --fixture loom/tests/fixtures/eval/jev_structure_pairs_v1 \
  --output /tmp/loom-method-panel-reproduction
python3 -m unittest discover -s loom/tools/structure/method_panel_v1 \
  -p 'test_methods_panel.py' -v
TMPDIR=/var/tmp python3 -m unittest discover -s loom/tools/structure \
  -p 'test_*.py' -v
```

The broad discovery command assumes a checkout containing current tracked files,
not unrelated untracked old-workspace copies. The session's corrected full run
used `run_current_remote_regression.py` plus the remote-file index to verify every
selected test Git blob and exclude only two unrelated old modules.
`current_remote_test_inventory.json` records the precise scope.

`first_run/first_scores.jsonl` has 48 raw score rows and source hashes, without
labels in the scoring input. `source_extractions.jsonl` preserves both frozen
extractor outputs for 64 unique source passages, exact text/spans, candidate
qualifiers, provenance/unknowns and coverage. `first_results.json` carries all
four predeclared operating points, group denominators, rank metrics, errors,
abstentions and disagreements. `run_manifest.json` hashes the original inputs,
protocol/policy/dependencies and marks supplied structural annotations unavailable.
`evidence_manifest.json` hashes the four first-run artifacts.

`embedding_addendum_results.json` evaluates the child's untouched frozen cosine
scores; `score_frozen_embedding.py` checks the raw artifact SHA-256 before using
labels. Its original vectors/scores/encoder protocol live separately in
`local_embedding_panel_v1/`; no trained weights or external services are required
to score this addendum. The scorer creates a new file and refuses overwriting.

`reproducibility_check.json` records exact recomputation and hash verification.
Initial broad discovery errors, corrected current-remote regression and new
mechanism test logs are preserved. The final artifact manifest excludes itself
and generated Python caches. No production source, graph store or existing
quality gate is modified by this directory.
