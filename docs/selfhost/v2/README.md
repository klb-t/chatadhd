# Knowledge pipeline measurements — 2026-09-28

The six-stage knowledge pipeline completed both the fictional export corpus
and a repository-text selfhost run. Full import now sends every imported unit
to extraction. The selector still misses most labeled relevant conversations,
and the generated repository profile has substantial semantic noise. These
results establish execution, scope and reproducibility, not production-quality
personal knowledge or predictive accuracy.

## Final synthetic measurements

Both protocols use the same frozen native executable, SHA-256
`c57e0b022a642dbb4ef26d83ff7bef57c2751a616923cdb00471eea9025253a2`,
pipeline **4**, scanner **3**, and the current embedded pack. The corpus is
fictional `synthetic_dev`; its project aliases are explicit input hints. This
is a development corpus, not a blind evaluation. Scoring protocol regressions:
**19 tests passed**.

| Measurement | Default selective | Controlled full import |
|---|---:|---:|
| Scanned units | 68 | 68 |
| Units actually extracted | 16 | 68 |
| Extraction failures | 0 | 0 |
| Observations | 108 | 346 |
| Final claim rows | 146 | 385 |
| Labeled-conversation selector recall | 13/45 (28.89%) | 13/45 (28.89%) |
| Labeled-conversation selector precision | 13/13 (100%) | 13/13 (100%) |
| Selected unlabeled provider documents | 3 | 3 |
| Selected noise traps / generic conversations | 0 / 0 | 0 / 0 |
| Project recall | 100% | 100% |
| Canonical alias recall | 24% | 24% |
| Decision recall | 9.52% | 57.14% |
| Fork recall | 0% | 100% |
| Principle recall | 30.77% | 84.62% |
| Operator recall, distinct example decisions | 0% | 16.67% |
| Claims with structural evidence violations | 0/146 | 0/385 |
| Materialized products | 31 | 128 |

The 68 units comprise 65 conversations and three provider project/memory
documents. Full import deliberately includes all 68 despite the selector's
16-unit recommendation; the selector metrics are unchanged. Both original
ZIP source hashes were retained in that run. It improves downstream coverage,
but does not solve the remaining selector or semantic extraction defects.

The ordinary selective A/A' runs have identical run IDs, all six stage hashes
and all 31 product files byte for byte. Full import was measured once; its
determinism was not separately measured. Its SQLite integrity check returned
`ok`.

Raw scorecards: [selective](synthetic-selective.json) and
[full diagnostic, including exact config](synthetic-full.json). The full run
uses the same `base_config()` as the selective harness with
`stage_params.catalog.import.mode = "full"`; no cut is applied. The ordinary
paired protocol remains unchanged.

These metrics have deliberately limited meanings. Structural evidence checks
validate support, quotations, expected properties and premise classes; zero
violations is not a semantic truth or hallucination rate. Version and status
coverage currently pools labels across projects/features, so it is approximate.
Operator recall now requires two distinct ground-truth example decisions;
an observation and claim for one decision cannot count twice. Precision excludes
unlabeled provider documents rather than silently treating them as negatives.
Calibration error is unmeasured.

Run B sees the full corpus while cutting seed priors, so its score is only
retrospective consistency. **Strict temporal predictive accuracy is unavailable**:
an exact historical pack loader without current-pack fallback and separate
post-cutoff scoring are still required. No real holdout key was read.

## Repository discovery diagnostic

This separate run used **pipeline 3 / scanner 2**, executable SHA-256
`6083b400da1fd20cd1c889ca3fb5b782b8a070b13cf047c77100b55e6959bbbb`.
It predates the final scanner repair and catalogue scoring changes above;
it is not a measurement of the final binary.

Its input was a staged snapshot of **283 tracked UTF-8 files, 3,260,871 bytes**,
local revision `8e50b044fffb6356b399c9cdf14205dd5815445b`. This local history was
reconstructed from GitHub API file snapshots based on upstream
`3219f88e7e90ab78eabeb80ef77bf7a7b55fecbc`; it does not establish authentic
historical lineage. The native run received only the staged files, without
`.git`, private conversation exports or a repository-history argument.

The staging policy excludes known evaluation/fixture/materialized outputs,
policy-pack source files, archives, unsupported files and marked generated
directories. Historical prose and handoff summaries remain eligible source
text. The current embedded pack and owner hints are used. This is therefore
repository-text discovery, not an independent historical benchmark or a full
archive analysis. Exact input hashes are in [inputs.sha256](inputs.sha256).

| Stage | Seconds | Result |
|---|---:|---|
| Catalog | 15.809 | 169 selected from 283 units |
| Extract | 52.784 | All 169 selected units; 14,602 observations; zero failures |
| Resolve | 7.903 | 244 merges; 4,616 claim rows |
| Assess | 4.008 | 22 contested claims in two conflicts |
| Generalize | 263.791 | 251 instances; 4,104 absence claims; 3 operators |
| Materialize | 11.192 | 251 dossiers + 251 extrapolated specs + SELF + BACKLOG |

Total wall time: **355.535 seconds**. Generalization used approximately 74% of
the elapsed time; the run completed rather than hanging. Final stored counts:
8,928 claims, 506 principles, three operators and ten predictions. Counts include
retained rejected/superseded entities and claims; they are not counts of verified
facts. No prediction had an evaluated outcome. Database integrity was `ok`.
Stage hashes, timings, exclusions and totals are in
[selfhost-summary.json](selfhost-summary.json).

The 504 generated products total 1,460,670 bytes; their complete SHA-256 inventory
is [products.sha256](products.sha256). Only small review samples are checked in:

- [SELF excerpt](SELF_excerpt.md) — includes code fragments such as
  `const Json& d` incorrectly appearing as projects.
- [BACKLOG excerpt](BACKLOG_excerpt.md) — preserves unresolved contested rows.
- [ChatADHD software dossier](dossier_in_9759155d0e77331e.md) — shows observed,
  inferred, proposed and absent slots, but its computed version must not be
  accepted as a verified current release.
- [ChatADHD music dossier](dossier_in_1f1c5ccf5c9c0a0c.md) — preserves a concrete
  spurious paradigm match for diagnosis, not an endorsed classification.

These visible errors make the next quality work concrete: improve selective
recall without weakening trap rejection, reduce code-fragment project names and
cross-domain paradigm matches, and profile principle discovery before scaling
to large archives. No threshold was lowered to make this report pass.

## Reproduction

From a built checkout, use fresh work/output directories:

```sh
python3 loom/tools/eval/knowledge_eval.py synthetic \
  --loom loom/build/dev/cli/loom --work /tmp/loom-eval-new \
  --out /tmp/loom-scorecard.json --markdown /tmp/loom-scorecard.md

python3 loom/tools/eval/knowledge_eval.py selfhost \
  --loom loom/build/dev/cli/loom --repo . \
  --work /tmp/loom-selfhost-new --out /tmp/loom-products-new
```

The second command stages that checkout's tracked HEAD, not dirty working-tree
files. A current checkout/binary will be a new measurement; reproducing the
diagnostic exactly requires its recorded input manifest and earlier binary.
Absolute source paths participate in run identity. The report preserves the
measured versions and hashes rather than claiming every later checkout will
produce these run IDs.
