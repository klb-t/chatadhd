# Offline archive text audit evidence — 2026-10-04

This directory contains public synthetic evidence only. It contains no export
or database, conversation text from the owner, credentials, or private URLs.

The frozen `archive_cost_before.py` is byte-identical to
`loom/tools/eval/archive_cost.py` at public base
`161cc22dfb84fe863389d6b90323bd44516a68dc`, SHA-256
`aa67b075661eb810785f7fc9d95d9acb7c7fa6df407e159db6037e30a27db5e3`.
It is included solely to reproduce `archive_stats`; its historical default
pricing path is irrelevant to this benchmark and is not a current-price source.
The benchmark implementation is read from `loom/tools/eval/archive_cost.py`;
its measured source is retained at commit
`f306eb0d7730531d094c6e291073449bc7720fdb`, SHA-256
`544af971164e0d4a25b5a64f0fc7cd8f220a62a26f45eadc12356b57a872c870`.
The later estimate-output clarification replaces `local_import_cost_usd` with
`model_calls: 0`, `local_import_model_cost_usd: 0` and
`local_compute_cost_usd: null`. It changes no `archive_stats` operation or
benchmark measurement. The saved numeric receipts remain the original runs;
test logs were rerun for the clarified labels.

## Measured result

A generated SQLite database had 600,000 messages, 300,000 conversations,
128 Unicode characters per message, and size 102,531,072 bytes. Status cycles
through active/version/excluded/deleted/NULL; every seventh role is tool.
Both final runs used the same synthetic database in Linux tmpfs, with SQLite
scratch storage also on tmpfs, and a fresh Python process for each run.

| Measurement | Frozen baseline | Current audit |
|---|---:|---:|
| Wall seconds | 2.155094692 | 6.482911600 |
| Process CPU seconds | 1.650863 | 5.460558 |
| Peak process RSS, KiB | 190,512 | 28,072 |
| Legacy nondeleted message count | 480,000 | 480,000 |
| Active characters | 15,360,000 | 15,360,000 |
| Explicit raw message count | Not reported | 600,000 |
| Explicit raw characters | Not reported | 76,800,000 |
| Exact selected-conversation presence groups | Not reported | 15 |

Peak RSS fell **85.3%**. The complete audit was **3.0 times slower in wall time**
and **3.3 times slower in process CPU**; this is a memory/completeness tradeoff,
not a throughput improvement. The baseline fetches all nondeleted message rows
and retains a Python object per conversation. The current audit counts every
stored row, includes status/role/tool breakdowns and selected-conversation
counts, and preserves the old exact median/maximum summary. SQLite stores
row-local text counts in temporary files and can spill grouping/sorting, while
Python retains category aggregates and at most 1,024 conversation-presence groups.
One individual message can still be materialized while counting its text.
Timing is environment-dependent; these are single measured runs, not a latency
distribution or a private full-export measurement.

Raw scope includes every stored status. A model-reading projection can select
all, active, or the historical active+version+unknown scope, and can independently
exclude tool/function rows. The historical top-level aliases still omit deleted
rows; use `raw`/`projected` for explicit counts. Unicode counts include embedded
NUL. Tokens remain configurable character-based estimates, not tokenizer/billed
measurements; retained source JSON, attachments/OCR, framing and hidden prompts
are outside this text-only scope.

No network/model calls occurred. Without caller-supplied prices the new CLI
returns `models: null`. Local import provider cost is zero; local compute/storage
costs are not measured. An explicit historical pricing file or explicit prices
can be used for planning, with no claim that those prices are current. Prefix
cache eligibility and table batch discounts remain unverified assumptions.

## Reproduce

From the repository root, using Python 3.10+ on Linux or macOS:

```bash
python3 docs/reports/archive-import-2026-10-04-evidence/audit/test_archive_cost.py -v
python3 -m unittest discover -s loom/tools/eval -p test_archive_cost.py -v
python3 docs/reports/archive-import-2026-10-04-evidence/audit/benchmark.py baseline --db /tmp/loom-audit-600000.db --create
python3 docs/reports/archive-import-2026-10-04-evidence/audit/benchmark.py baseline --db /tmp/loom-audit-600000.db
python3 docs/reports/archive-import-2026-10-04-evidence/audit/benchmark.py current --db /tmp/loom-audit-600000.db
```

Choose a fresh external `--db` path: generation refuses to overwrite a file.
The benchmark uses generated placeholders only, and deliberately keeps its
large database outside the repository. `--repo PATH` supports running a copied
benchmark; the regression script locates the repository from its own parents.
For the original tmpfs setup, substitute `/dev/shm/loom-audit-600000.db` on
Linux and set `SQLITE_TMPDIR=/dev/shm` when running each benchmark. The final
receipts are retained exactly; do not overwrite them with new timings.

`existing-tests.txt` records 11 passing existing sentinels. `extra-tests.txt`
records 8 passing additional synthetic regressions, and `portable-tests.txt`
records the successful rerun of the copied portable script. The additional
cases cover full inventory, Unicode/NUL, exact scope/tool conversation counts,
absent-price behavior, configurable prices/estimator, invalid estimator/cache
settings, read-only file preservation, and an empty database.
