# Stage 1 final public snapshot — immutable v2

All 432 planned operations are represented by 432 unique captured response projections and generation IDs. The existing pure scorer reproduces all eight reports, failure lists, slices and seven paired comparisons exactly: 384 composed source-commitment decisions over 12 authored DEV cases with four queries each, with 48 available decisions per scored arm. Public cost representations agree exactly at **USD 0.052296100**. This audit concerns authored, matched DEV source commitments; content/world truth and heldout/general model quality are unmeasured.

[`SCORE.json`](SCORE.json), [`VERIFICATION.json`](VERIFICATION.json) and [`packet.json.gz`](packet.json.gz) remain standalone original files. [`replay-inputs.zip`](replay-inputs.zip) contains the other 14 original JSON files plus `REPLAY_MANIFEST.json`, which records exact SHA-256 and byte sizes for every payload and all 17 originals. Packaging preserves every original byte. The ZIP is 534,204 bytes; its 14 plain payloads total 8,815,456 bytes. This storage choice avoids separately publishing duplicate plain snapshots; it is not a new size limit or an omitted-data policy.

To restore the evidence paths from a fresh checkout, run from the repository root:

```bash
python -m zipfile -e docs/research/model_research_2026-10-04/programme-actual/stage1-final-20261005-v2/replay-inputs.zip docs/research/model_research_2026-10-04/programme-actual/stage1-final-20261005-v2
```

Extraction must target this same directory because `configuration.json` references the exact original evidence paths. It restores `METHODS.json`, `NORMALIZED.json`, `REQUESTS.json`, `RESPONSES.json`, `configuration.json` and all nine group reports without changing their identities or hashes. The original producer receipt is unchanged; added storage/audit files are recorded separately.

ZIP SHA-256: `7f7ae287e6c6644427e89ea009cb17577816d230cf9b56915048a0f63c19cefa`. Canonical packet ID: `c79b6762459f94c0e10562d4bb34a3540af287abf6964d402d4025c50fadf42f`. The 36,883,401-byte raw packet regenerates exactly; the deterministic 2,180,141-byte gzip also matches byte-for-byte. It contains 2,292 entities, 6,336 claims, 14 evidence observations and 432 event records, with no ModelProfile entities or canonical-store write.

| Scored recipe | Correct / planned | Physical calls | Billed USD |
|---|---:|---:|---:|
| `j_active` | 40/48 | 48 | 0.001699908 |
| `j_directed` | 43/48 | 48 | 0.001899492 |
| `j_roles` | 43/48 | 48 | 0.002117556 |
| `g_brief_t0` | 32/48 | 48 | 0.009852400 |
| `g_brief_t03` | 33/48 | 48 | 0.009852400 |
| `g_rules_t0` | 37/48 | 48 | 0.011881200 |
| `g_rules_t03` | 38/48 | 48 | 0.011879600 |
| `j_split` | 43/48 | 96 | 0.003113544 |

`j_directed`, `j_roles` and composed `j_split` tie at 43/48; `j_directed` has the lowest billed cost within that tie. `g_rules_t03` reaches the local GPT-family maximum of 38/48. These are task-specific W1 candidates, not statistical or Stage5 winners. The leading Jev arms still wrongly refute 5 of 24 unknown cases; GPT’s local best wrongly refutes 10. [`stage1-measured-recipes-for-w1-v1.json`](../../stage1-measured-recipes-for-w1-v1.json) retains exact existing recipe material, parameter/version hashes, requested versus observed identities, adapters and all consumed body references. No new body or compiler is invented.

The split recipe uses two physical calls per decision. Its composed semantic score is preserved in the full SCORE evidence observation. The current packet has no composite evaluation node; standalone split-component accuracy remains null. Latencies are host-measured first responses per call, with heterogeneous original transport/billing cohorts, not isolated provider inference time or a composed split-decision wall time.

The original cohorts were three completed, three uncertain and 426 pending-billing rows. Append-only proofs resolved 429 rows; all 432 effective rows now report completed/verified and no normalization errors. The execution owner reports that a strict fresh account read matched the same USD sum and left zero pending/reservations. Public replay verifies selected fields, exact synthetic request bytes, semantic metrics and graph evidence. It does not independently authenticate complete private raw captures, generation bindings, account data or absence of unpublished retries. The owner retains all original generation proofs separately. Public projections intentionally omit credential/account/personal metadata; their hashes preserve references to the private originals and do not undo that projection loss.

All v2 producer/scorer hashes resolve exactly at Git source snapshot `e71e30aa26bbf0ce22b74c386627d9a2d8a9c373`; frozen-preparation source drift is empty. The [public audit receipt](../../verification/stage1-final-public-audit-v1.json) records all 17 original file hashes, byte replay, source versions and the independent public-only review. It does not silently repair the separately recorded earlier v1 archive provenance gaps. Audit/export itself made no provider calls and changed no code/tests. Stage5 corpus/labels were not read and no Stage5 winner was selected.
