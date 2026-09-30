# Complete independent mechanism evidence

`MECHANISM_EVIDENCE.zip` contains all first observations, synthetic fault/concurrency
ledgers, every re-probe and log, both source closure ZIPs, protocols and scripts.
446 payloads, 1411759 original bytes; container
513340 bytes, SHA256 `6632ab8dbf4d906ebf0a821c428f000540d0bf01dcc861a60d69c5e3faeba1a9`. Every member
was reread and matched unchanged original bytes. Locks, caches and loose reviewed
source duplicates are excluded; canonical source ZIPs preserve those source bytes.

Use the tested hash-checked zero-overwrite restore script from
`docs/research/graph_free_extraction_v1/FIRST_ARCHIVE.md`, passing this ZIP,
`MECHANISM_ARCHIVE.json` and a new destination. After restoring this outer archive,
restore either source closure ZIP and its matching receipt into ANOTHER new
destination to reproduce original or final code. Do not overwrite a checkout.
The explicit scripts and interpretation are in `AUDIT.md`. All ledger money
values here are scripted counters; no live API spend or model-quality measure.

Loose evidence remains locally available, but Git may preserve it through the
canonical archive; the stage manifest excludes duplicate ledgers/logs and obsolete
loose source/test snapshots.
