# Knowledge-layer evaluation

The harness uses Python's standard library and the public `loom knowledge run`
CLI. It does not read the real holdout answer key. Run from the repository root:

```sh
python3 loom/tools/eval/knowledge_eval.py --help
python3 -m unittest discover -s loom/tools/eval -p 'test_*.py' -v
```

## Synthetic development scorecard

```sh
python3 loom/tools/eval/knowledge_eval.py synthetic \
  --loom loom/build/dev/cli/loom \
  --work /tmp/loom-eval \
  --out /tmp/loom-scorecard.json \
  --markdown /tmp/loom-scorecard.md
```

Requires `loom/tests/fixtures/eval/synthetic_dev/ground_truth.json` and its
ChatGPT/Claude ZIP fixtures. This is a development corpus with fictional labels;
project aliases from its answer key are explicit input hints. It is not a blind
test. Optional `--floors FILE.json` applies numerical/boolean gates (exit 1 for
failed gates; unavailable metrics fail gates).

Runs A and A' measure extraction and repeatability. Run B sees the **entire**
corpus with `prior_cut=T`: that setting cuts seed priors, not source records.
Its output is therefore named `retrospective_consistency`, with
`solution_class_match_rate` and `predictive_accuracy: null`. It must not be
reported as temporal prediction accuracy. Old floors referring to
`holdout.prediction_accuracy_solution_class` intentionally fail; they must not
be silently treated as an equivalent benchmark.

## Real self-discovery

```sh
python3 loom/tools/eval/knowledge_eval.py selfhost \
  --loom loom/build/dev/cli/loom --repo . \
  --work /tmp/loom-eval --out /tmp/loom-products-new
```

This runs on a sanitized **tracked HEAD snapshot**, using current built-in
policy/owner hints. It is discovery, not measured accuracy. Commit relevant
changes before running: untracked files and working-tree modifications are not
inputs. The output directory must not already exist. Products include the input
manifest (revision, blob IDs, SHA-256, exclusions) and raw run result.

All commands create a fresh private directory beneath `--work`, preserve it for
inspection, and never clear the directory supplied by the caller. Existing
products are not overwritten. No network download or archive extraction is
performed by this harness.

## Historical source staging / holdout status

```sh
python3 loom/tools/eval/knowledge_eval.py holdout \
  --loom loom/build/dev/cli/loom --repo . --cut 2026-03-06 \
  --work /tmp/loom-eval --out /tmp/cut_2026-03-06.json
```

**Strict temporal prediction is currently unavailable (exit 2).** The command
writes an explicit `status: unavailable`, `benchmark_valid: false`, empty
predictions and `predictive_accuracy: null`. When history permits, it stages
historical sources and records their manifest/work directory. It does not run
the model on a contaminated substitute. `--products` is reserved for a future
isolated prediction run and currently creates no products.

The runner loads a current embedded pack; owner aliases and other policy data
can contain hindsight even with `prior_cut` or `priors=false`. A valid future
implementation needs a complete, independently selected pre-cutoff pack loaded
**without fallback or merging with the current built-ins**, plus separate
post-cutoff scoring. Source isolation alone cannot establish a predictive
benchmark. The current engine/heuristics are also retrospective code; claims
about historical deployed performance would need a frozen evaluation protocol.

Staging rules:

- Cut dates are inclusive UTC days. Select the newest eligible commit on HEAD's
  first-parent history. Both author and committer dates of that commit and all
  its reachable ancestors must be on/before the cut.
- Never inspect `--all`, another branch, the working tree, reflogs or answer-key
  blobs. An old date mentioned in a newly added document does not qualify it.
- Shallow history or no eligible snapshot produces an unavailable result, not
  guessed history. Git timestamps are repository evidence, not external proof
  against forged/backdated history.
- Copy regular, tracked UTF-8 text/code files only, at most 16 MiB each. Skip
  symlinks, submodules, archives, binary/LFS pointers and unsupported extensions.
- Exclude evaluation/tests/fixtures, answer-key/ground-truth paths, selfhost and
  generated/product/prediction/result directories, materializer filenames,
  hidden files, policy pack source data, and directories marked `.loom-archive`.
  Excluded blobs are never opened; their paths may appear only in Git listings.
  Filtering is conservative: exclusions reduce coverage, so manifests report
  it instead of claiming the whole archive was evaluated.

Repeat `--cut` for several cuts; then `--out` denotes a directory. Successful
command exits are 0, synthetic gate failures are 1, and unavailable/invalid runs
are 2. An empty prediction list with `status: unavailable` is **not** a measured
zero-accuracy result.
