# First-response scoring of the new source-label population

Status at source freeze: ready before reading any actual model response. The helper,
configuration, scorer dependencies, gold container, source inputs, prepared inputs,
and all 192 exact request bodies are bound in `SOURCE_FREEZE.json` (202 files).
The helper and configuration were already written before collection; closing this
preparation did not change their bytes or the frozen probability threshold.

This is an offline replay of one first attempt per operation for `j_active` and
`j_directed`: 96 authored queries per method, sharing 24 source families (12 Polish,
12 English). Missing, invalid and conflicting first labels remain in the planned
denominator. A duplicate operation, generation, successful response capture, changed
gold, request or source binding causes a visible rejection. There is no retry or
selection of a later response. Unknown cost remains null.

The instrument loads only hash-pinned pure AST symbols from `graph_panel_live.py`
and `_keys` from `openrouter_runner.py`; it does not import their orchestration or
read their historical fixtures. The original strict rule is unchanged: support if
q01 > 0.5, refutation if q02 > 0.5, both above means conflicting/unavailable, neither
above means unknown. Data counts describe this authored cohort and are not runtime
limits.

Quality means agreement with the latest active explicit source commitment for the
specified relation, ordered operands, speaker and time. It is not world truth,
semantic adequacy, native graph grounding, real-conversation adequacy or sealed
holdout performance. Source and gold were author-visible. Report each method's
planned query and family denominators; family, language and relation groups are
overlapping views and their costs must not be added again.

## Replay

Run from the repository root. Supply a public normalized bundle exported by the
existing strict programme runner, and its independently recorded SHA-256. No key,
private capture or ledger is needed by this helper.

```sh
python docs/research/model_research_2026-10-04/stage5/new-label-scoring-v1/score_first_only.py \
  --config docs/research/model_research_2026-10-04/stage5/new-label-scoring-v1/configuration.json \
  --manifest docs/research/model_research_2026-10-04/stage5/new-label-preparation-v1/prepared/manifest.json \
  --bundle PATH_TO_PUBLIC_NORMALIZED_BUNDLE \
  --gold docs/research/model_research_2026-10-04/stage5/new-label-corpus-v1/gold.json \
  --config-sha256 ebc02a4cef3a5c02189815d69ab09217a73d2d240a638f3dd127ca920004bd41 \
  --bundle-sha256 RECORDED_NORMALIZED_BUNDLE_SHA256 \
  --output PATH_TO_NEW_SCORE_JSON
```

Output paths are immutable: a later replay may confirm identical bytes, but cannot
replace an existing different score. Before reading actual outputs, record this
helper's SHA-256 `51a68636c569362e6777865467e6eace09ff9d79c7f7613a53a3132d7a782d13`
and the configuration SHA-256 above in the published preparation commit.

## Fake-only verification and preservation

`python docs/research/model_research_2026-10-04/stage5/new-label-scoring-v1/test_score_first_only.py`
passes eight protocol controls: changed gold hash, duplicate first row, duplicate
generation, duplicate successful capture, verified cost mismatch, missing source
endpoint, missing first response, and invalid q02. The last two retain the original
planned denominator, and cost is kept separate from label availability. The positive
four-attempt baseline gives exact labels and exact verified fake cost.

`FAKE_VERIFICATION.json` records the latest run (2026-10-05T15:54:57Z); earlier
attempts and every negative fixture are preserved in
`fake-protocol-controls-full-20261005.zip` (312 payload files plus SHA-256 manifest,
322,898 bytes, archive SHA-256
`811903440ffae92b3982639ab1c5473649612b4135cfda740e10780776d006b5`). Archive CRC
and unique relative paths were checked. Historical fake configurations retain their
original absolute dependency paths; rerun the portable test to create equivalent
controls in another checkout, using the frozen dependency hashes. All fixtures are
fictional: no real response, private ledger, secret or model call was used.

Billing numbers rely on the runner's transport/ledger attestations and public
generation projections; this scorer validates their bindings and agreement, and
does not independently reconstruct private transport captures. Actual results are
to be written separately after collection; this preparation contains no measured
model quality claim.
