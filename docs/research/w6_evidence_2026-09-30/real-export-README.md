# Actual owner export: bounded offline validation

Production baseline `b118c80e981c08ec6d7f9ab6aacc177979186cf2`. The original
first instrument and first aggregate outputs are preserved alongside this file.
Independent reviewer requested immutable output directories; the current runner
now refuses an existing output directory and includes its own SHA. This changes
receipt handling only, not the already run comparison. First instrument is saved
as `real-export-first-instrument.py`; no second real-data measurement is claimed.

Connected Library search found derived catalogs, not source export JSON.
Drive browsing located September 2026 OpenAI shards and a 206,518,714-byte
Anthropic conversation JSON. The OpenAI shard chosen by the protocol was fetched
as original bytes. Anthropic raw fetch returned HTTP 504 (`MCP request timed
out`) without a usable file reference: real Anthropic arm **not evaluated**.
This is an access result, not proof that the file is malformed or unavailable
through every other route. No new file request is necessary to resume it.

Observed OpenAI slice (1,308,663 bytes):

- 100/100 reconstructed conversations exact, including all 8,947 scalar values
  and 73 empty containers (9,020 typed path/value atoms).
- 730/730 raw message objects exact; 685 active and 45 version rows observed.
  These status counts are descriptive, not an independent branch-status oracle.
- Original source blob byte-for-byte equal. No added/missing/changed JSON atoms.
- Second CLI process reopened successfully; repeat import created no additional
  conversations/messages and preserved every message ID/metadata pair.
- 730 message provenance rows, zero JSON Pointers. The import path stores
  conversation/message ordinals. Missing pointers does **not** mean the source
  cannot be located using a join with `metadata.export.key`; exact standalone
  provenance locators are the separate synthetic finding.

No attached asset bytes were included. This does not validate all archive files,
attachment resolution, semantic interpretation, whole-export scale, imported
provider requests, or real model behavior. Availability-driven sample, not a
representative or protected holdout. Only local parsing and native import were
used. The private source and runtime DB/logs remain outside the public repo.

Reproduce with an authorized local copy matching the aggregate source hash:

```bash
python3 loom/tools/w6_evidence/real_export_audit.py \
  --cli /absolute/path/to/baseline/cli/loom \
  --source /private/path/conversations-000.json \
  --runtime /private/path/new-runtime \
  --out /private/path/new-aggregate-result-directory
```

The original run used `--out docs/research/w6_evidence_2026-09-30` before the
immutable-output improvement. The source hash, binary hash and measured counts
are in `real-export-before.json` / `real-export-first-result.json`. Private
paths/Drive IDs are omitted intentionally; the source hash identifies the bytes.

Independent synthetic review also found that the original `pointer()` accepted
noncanonical array indices (`-1`, `01`, `+1`). The active runner now rejects
those indices and malformed `~` escapes. Original review controls/results remain
in `transport_review_real_export.json`; follow-up controls are separate. The
actual shard had zero pointer fields, so this changes no real-data comparison
or measurement. Pointer presence is not being promoted into locator correctness.

The offline label records the deliberately keyless, semantic-disabled execution
configuration. This run did not include a packet-level egress monitor; its
`provider_requests` field is not an independently measured network counter.
