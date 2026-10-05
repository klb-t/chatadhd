# Fixed case-paired admission: new frontier cohort

This is an offline partition of the already frozen twelve first requests. It does not change the scientific selection, method, parameters, request body, complete operation row, source row digest, physical operation ID, or order. The methods address **different tasks**: GPT pattern discovery and Gemini graph completion; this is not a same-task model ranking. All six pairs are declared before any frontier response is read.

| Order | Source case | Operations | Conservative reserve USD |
|---:|---|---:|---:|
| 1 | f01-pl-originals | 2 | 0.676508750 |
| 2 | f02-pl-rehearsal | 2 | 0.691755475 |
| 3 | f03-pl-parameters | 2 | 0.706527050 |
| 4 | f04-en-editorial | 2 | 0.691140575 |
| 5 | f05-en-cargo | 2 | 0.690553625 |
| 6 | f06-en-conservation | 2 | 0.705171475 |
| Total | Entire frozen cohort | 12 | 4.161656950 |

The master reserve exceeds the previously verified remaining 4.126783500 USD by 0.034873450 USD. Partitioning does **not** make the full worst-case cohort affordable. Each pair is admitted by the existing cumulative private ledger: actual charges of previous pairs plus existing in-flight reservations plus the next exact pair reserve must fit the unchanged key cap of 5 USD and owner authorization of 5 EUR. Finish read-only billing reconciliation before admitting the next pair. Stop before the first unaffordable pair; never skip it to run a later attractive case. Unexecuted cases remain missing observations with an explicit budget stop reason, and remain in the preregistered inventory. No retry or replacement is granted here.

Each `pair-XX/prepared/manifest.json` has a unique stage ID and the original physical operation IDs. Its two request artifacts and the full original `source-prepared.json` are exact copies, not recompilations. `PARTITION.json` binds the source selection, manifest, policy, forecast, all child hashes, fixed order and all twelve unchanged rows. Child `cost-plan-v1.json` preserves the same quote evidence, all charge components, separate per-operation reserves and explicit unknown actual cost. These are forecasts rather than billing evidence; quote/key/FX freshness and existing admission guards are checked again by the payer.

Source manifest programme ID is `new-frontier-corpus-20261005`, while the source operator policy binds the cumulative key programme `thread7-new-key-2026-10-04-eur5`. The existing invocation preparation must bind child programme IDs to that cumulative programme, preserve physical IDs and bodies, add provenance, and freeze invocation hashes before any dispatch. This directory does not reset accounting, open a key or make that private invocation automatically.

`CLEAN_REPLAY.json` proves all 37 generated artifacts replay byte-for-byte in an independent temporary directory and all six manifests pass the native strict loader. `OFFLINE_TESTS.log` contains seven passing controls for identity collision, missing/distinct method membership, order, stale source hashes, conflicting pre-existing output and unchanged paired inventory; no model response is used. These are tests of admission preparation, not model-quality results.

From repository root, reproduce offline:

```sh
python3 docs/research/model_research_2026-10-04/stage5/new-frontier-paired-admission-v1/partition.py --config docs/research/model_research_2026-10-04/stage5/new-frontier-paired-admission-v1/configuration.json --output /tmp/thread7-frontier-paired-replay
python3 docs/research/model_research_2026-10-04/stage5/new-frontier-paired-admission-v1/test_partition.py
```

The reusable helper contains generic partition, hash, exact-copy and validation operations. All case names, methods, pointers, stage naming and source pins are in `configuration.json`. No new stage 4 or reply experiment is part of this artifact.
