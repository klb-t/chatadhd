# Real export slice protocol — frozen before import

Source selection is availability-driven, not a representative random sample.
Search connected Library/Drive before requesting any new input. Use the
lexicographically first numbered OpenAI JSON shard in the September 2026 export
folder. Preserve exact bytes. Read structures/counts, not unrelated conversation
meaning. Run native importer in a fresh local data directory, without keys or
background workers. Select explicit `--export-mode on` for a bare JSON shard.

Before running: record source and CLI hashes, bytes, scalar-leaf and
empty-container counts, conversation count, graph-node and nonnull-message
counts. After running: independently compare every stored raw message by export
key and reconstruct conversation objects using stored graph metadata. Compare
typed JSON values and paths; do not accept importer counters as an oracle.
Verify the stored raw-source blob bytes. Check source locators and branch
classification where supported. Reopen in a second process and reimport to check
persistence/idempotence. Preserve the first result, including errors/mismatches.
Do not repair production implementation from this lane.

Attachment references are assessed separately from available attachment bytes;
a JSON shard alone cannot establish attachment-resolution fidelity. Import
fidelity is not semantic understanding or request provenance. This sample is
not a sealed holdout and must never be reported as complete-archive validation.
No selected conversations are submitted to a model or committed to this public
repository; output is aggregate only. Local source path/Drive identity stays in
private scratch. Public receipt uses source hash and structural counts.

Anthropic: locate existing export metadata. Attempt raw materialization only
when available; if access/size blocks it, record the exact limitation and keep
that arm unevaluated rather than substituting synthetic evidence.
