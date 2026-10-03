# Import V2 exact structural oracle

Cross-review correctly identified holes in V1's leaf oracle: added fields did
not fail subset comparison, and numeric object keys could mimic array positions.
V1 artifacts remain byte-identical. Their scalar-path diagnostic counts remain
useful, but structural equality now relies on V2.

The amended instrument and protocol were frozen before another native run.
**23/24 named checks pass:** all **12/12 oracle controls**, **9/9 raw-message
structural comparisons**, and **2/3 complete conversation reconstructions**.
The preserved negative is Anthropic source-array order. Both OpenAI cases pass
exact recursive comparison including every container, added/missing keys,
array order, primitive type and value. This closes the two concrete oracle
risks; it does not expand the dataset or establish generalization.

The accompanying unchanged V1 rerun remains **47/59**, with the same named
negatives. These counts overlap and must not be added. Complete original-byte
availability and missing separate ZIP member blobs retain the interpretation
from the first report. No production changes.

V2 freeze SHA-256:
`bfef310f2b1985b758c4e8aec261bcd072a9fb59aeaf56761a010e1ea5edd8cb`.
Binary SHA-256:
`c08e869e687a757ba05ca6fd80e51e16d8d3282ba575294e4c5fb34ee0ce52fc`.
Baseline: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`.
Full outputs: `import_v2_run/receipt.json` and `import_v2_run/native/`.

## Text-only distribution

The frozen ZIP files can be represented by their `.zip.b64` companions without
changing a single archive byte or the original manifest. From repository root:

```bash
python3 docs/research/w6_evidence_2026-09-30/import_decode_fixtures.py
python3 loom/tools/w6_evidence/import_audit_v2.py evaluate \
  --binary /absolute/path/to/baseline/cli/loom \
  --out /absolute/path/to/new-empty-v2-directory
```

The decoder verifies decoded bytes against the existing frozen manifest,
refuses to overwrite a mismatched existing ZIP, and is harmless when matching
ZIPs are already present. Exact decoded originals:

| File | Bytes | SHA-256 |
|---|---:|---|
| openai.zip | 3817 | `ec1bd7edd3ab2bf5be81083619c3c24b7db3f6ea4624c0714f79f9c40b84a247` |
| anthropic.zip | 4647 | `eb95db149db772b4cadd91860a8255130e34e501227b3cf3840eebeb18cdbcca` |

The 7,628-byte member total in the first report is uncompressed member content,
not the 8,464-byte sum of ZIP container sizes. V2 intentionally reuses frozen
fixtures rather than calling V1 `generate`, which refuses an existing manifest.
