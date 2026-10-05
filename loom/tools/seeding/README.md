# Offline graph seeding prototype (R36)

Pure Python, no dependency installation, external calls or canonical graph
writes. It completes **supplied oracle graphs** from the synthetic development
fixture. Roles, operator applications as capability proxies, and exact corpus
features as module proxies are reported separately. This is not automatic
extraction of project modules from text or code.

```bash
python -m unittest loom.tools.seeding.test_prototype -v
python loom/tools/seeding/prototype.py --output /tmp/a-new-seeding-run
python loom/tools/seeding/prototype.py --output /tmp/a-new-local-run \
  --policy loom/tools/seeding/policy_local_premises.json \
  --protocol docs/research/SEEDING_LOCAL_PREMISES_2026-09-30.md
python loom/tools/seeding/prototype.py --output /tmp/a-new-alternatives-run \
  --policy loom/tools/seeding/policy_application_alternatives.json \
  --protocol docs/research/SEEDING_APPLICATION_ALTERNATIVES_2026-09-30.md
```

Runs refuse to overwrite output directories. JSON is losslessly archived as
gzip; read with `gzip.open(path, "rt")`. Each run freezes the protocol, policy and
code and identifies their SHA-256 hashes. Predictions precede scoring.

For exact historical code replay, restore `prototype_frozen.py` to the original
module path in an isolated checkout; its relative fixture paths expect that
location. The snapshot file is retained as provenance, not an in-place launcher.

The five saved run directories under `results/` are active regression fixtures.
`test_independent.py` reads their predictions, scores and frozen inputs to
independently recount metrics, verify provenance and hashes, and preserve
negative results. CMake includes this suite in `research.seeding`. The
2026-10-04 cleanup therefore retains all 26 result files (13,800,361 bytes) in
place; none qualifies as an unused run copy. Run both mechanism and artifact
checks from the repository root with
`python3 -m unittest discover -s loom/tools/seeding -p 'test_*.py' -v`.

Every random ranking is saved; random seed zero owns full candidate/provenance records,
and later seeds refer to that pool by label. First v1 JSON was compressed after
the run without changing bytes; `uncompressed_sha256.txt` verifies them.
The first v1/v2 historical snapshots explicitly labelled `known_at` as a donor
source date; this was corrected in the current schema. Source dates remain
`source_created_at_max`/locator dates, source know-times remain unknown, and
candidate `known_at` is its creation in the run. The metadata-corrected v2 run
has identical metric dictionaries and is retained separately.

## First measured outcomes

All versions use the same five project-level folds, 46 one-element masks and 15
no-removal controls. Data is fictional, development-only and already inspected
for schema design. V2 is a correction selected after v1 errors, not independent
validation. The original baselines remain unchanged, and later controls explicitly
match candidate eligibility. No temporal quality claim is possible.

Top-1; precision includes false positives on no-removal controls:

| Task | V1 mapping precision / recall | V2–V4 weighted mapping precision / recall | Unfiltered frequency precision / recall | Frequency on identical eligible pools | Random expected TP / positives |
|---|---|---|---|---|---|
| Observed roles | 7/23 / 7/18 | 7/23 / 7/18 | 8/23 / 8/18 | 8/23 / 8/18 | 2.1357/18 |
| Applied operator capability proxies | 0/15 / 0/10 | 6/9 / 6/10 | 0/15 / 0/10 | 6/9 / 6/10 | 1.9667/10 |
| Exact feature/module proxies | 0/23 / 0/18 | 0/23 / 0/18 | 0/23 / 0/18 | 0/23 / 0/18 | 0/18 |
| All | 7/61 / 7/46 | 13/55 / 13/46 | 8/61 / 8/46 | 14/55 / 14/46 | 4.1024/46 |

Independent review exposed a confounded baseline: V2 changed candidate
eligibility as well as ranking. V3's `premise_filtered_frequency` applies the
**same eligible pool**, then ranks by donor-project count. It reproduces every
capability top-1/top-3 prediction and wins overall (top-1 14/55, 14/46; top-3
16/149, 16/46). The improvement over the unfiltered baseline comes from premise
eligibility; no weighted-ranking benefit is established. V3's expected outcomes
were already known from independent re-ranking of V2 and its saved run is a
control/reproducibility check, not new validation.

At budget three, v1 operator recall rises to 7/10 (precision 7/43), above frequency
6/10 (6/43). This diagnoses candidate ranking rather than merely vocabulary
coverage. V1 roles lose to frequency at both budgets; no structural-role benefit
is claimed. Exact features all have unseen labels in the other projects, so an
observed-label transfer method has a hard recall ceiling of zero.

V2 keeps the local `decision → principle → operator` mapping. It recovers six
operator masks and abstains in six of the fifteen operator cases. Four masks
remain missed: the new-source operator and the uncertainty operator each occur
in only the target project, and two implementation-detail applications have no
recorded decision-premise edges. Three emitted non-matching candidates are
NoteFlow uncertainty proposals (including one no-removal control): broad
`small_reversible_steps` principle agreement is **insufficient evidence that the
operator's situation actually applies**. These proposals remain hypotheses.

Full per-fold/task metrics, 32 random repetitions, FP/FN labels and denominators
are preserved under `results/synthetic_lopo_v1_first/` and
`results/synthetic_lopo_v2_first/`. Candidate-vocabulary coverage is kept separate
from selection recall. Neither stable mappings nor passing mechanism tests are
semantic model-quality measurements.

V4 corrects a separate derivation-graph defect: independent applications
justified by pA or pB are preserved as separate alternative conjunctions, rather
than merged into pA AND pB. `preserve_application_alternatives` accepts a complete
nonempty conjunction from any application and carries its donor decision/source
refs. A partial conjunction still abstains. The historical union policy remains
explicitly available to reproduce V2/V3; defaults are unchanged. V4 all score
dictionaries are identical to V3 on this fixture: this is a tested semantic
correction, with no measured quality gain claimed. Its first output is under
`results/synthetic_lopo_v4_application_alternatives_first/`.

Decision: keep the candidate-only mechanism and optional local-premise policy;
do not promote the generator to production. Next useful work: separately model
and verify situation predicates (not just shared principles), generate new
target-specific templates rather than copying donor names, and test on a fresh
independent corpus. Keep the unfiltered and eligibility-matched frequency
baselines as active alternatives; weighted ranking has not earned preference.
