# W3 receipt — directed commitment and ModelProfiles

- base_sha: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`
- protocol_sha: `fd923be84ba27bea9da5c09d01b63dc25bf3365b`
- head_sha (verified implementation): `6d7861e9394769748005b791c06537d6945c46b1`
- branch: `gpt/w3-model-recipes-2026-09-30`
- integration: not performed; ROOT owns integration and STATE
- publication: blocked by automatic approval review; no remote W3 branch exists
- ready for review: 2026-09-30; exact start/integration time not measured

## Goal and result

Investigate the observed scope error where withdrawal of A→B incorrectly
refutes B→A, and withdrawal of denial is lost after unrelated speech. Added
a versioned question recipe, exact offline replay, existing-contract-compatible
task-specific profiles, and 48 independently authored DEV queries. Historical
96 original first responses replay exactly; profiles retain 42/48 and45/48.
These are old model results. New recipe quality remains **unmeasured**.

## Owned files

- `loom/tools/structure/w3_directed_commitment_v1/*`
- `loom/tools/structure/test_w3_directed_commitment_v1.py`
- `loom/tests/fixtures/research/w3_directed_commitment_v1/*`
- `docs/research/w3_directed_commitment_v1/*`
- this receipt

No existing implementation file was edited. No shared runner/ledger change,
schema/ABI migration, credential access, private source ingestion, sealed
validation read, main/PR6/integration edit, deletion or force push.

## Verification

Run from repo root; Python3.12 plus jsonschema4.26.0 in the isolated dependency
directory. Prefix commands with `PYTHONPATH=/workspace/scratch/47408c3bd5bc/deps`.

```sh
TMPDIR=/var/tmp python -B -m unittest loom.tools.structure.test_w3_directed_commitment_v1 -v
TMPDIR=/var/tmp python -B -m unittest discover -s loom/tools/structure -p 'test_*.py'
python -B -m unittest discover -s loom/tools/eval -p 'test_model_profiles.py'
python -B -m loom.tools.structure.w3_directed_commitment_v1.experiment verify --plan docs/research/w3_directed_commitment_v1/prepared_final
```

- W3 mechanisms: **20/20**, including **9** independently authored tests.
- Final structure regression: **841/841**, 36.972s; includes those20, do not add them.
- Existing profile contract tests: **35/35**, separate suite.
- Fresh archive-derived replay: **96/96** original model responses, no new calls.
- Actual historical source membership verified for **23** files, including ZIP.
- `git diff --check`: clean. Native build not rerun; no C++/ABI files touched.

Raw logs and machine-readable counts: `docs/research/w3_directed_commitment_v1/verification/`.
Independent review and fixes: adjacent `INDEPENDENT_REVIEW.md`. The original
full structure run before the no-repeat planning fix also passed841; its log
is historical, not an additional denominator.

## Boundaries and next step

New calls: **0**, new model cost: **USD0**. Costs of old responses remain once
per batch; generation invoice audits remain unavailable. Independent authored
DEV is not a blind holdout. Semantic correctness of the new recipe requires
actual first inference, not scripted parser tests.

Only `prepared_final/` is current. The old48-call comparator is disabled for
live execution and reused offline. Three prospective48-call batches reserve
USD0.144 total, inside the existing shared USD2 programme only after ROOT
checks current key, spending, uncertain charges and authorization. They were
not executed. The first preparation remains for provenance and fails the
current code freeze; never use it to initiate calls.

Next: approve scoped public branch publication, ROOT reviews/integrates these
commits, then selects the next authorized model comparison. Preserve all first
results and report paired errors/regressions before deciding whether v3 helps.

## Publication blocker

The first `git push -u origin gpt/w3-model-recipes-2026-09-30` was rejected by
automatic approval review: public disclosure of the protocol was considered
not explicitly authorized. No alternate connector/API push was attempted.
The remote integration head was checked again and stayed at base_sha. A local
transfer package retains commits and evidence; it is not a remote integration.
