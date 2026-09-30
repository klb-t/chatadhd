# Independent review of real export audit

Reviewed `loom/tools/w6_evidence/real_export_audit.py` without opening owner input
or runtime data. The public result schema contains aggregate counts, content
hashes and status names; raw input, message text, locators and private paths are
not emitted. Native stdout/stderr remain in the private runtime. Full source
blob byte equality and type-aware reconstructed JSON equality are measured
separately from normalized chat content. Attachment-reference JSON retention
does not claim attachment-asset verification.

Independent synthetic helper controls: **8/8 positive controls pass**, including
bool versus integer, integer versus float, empty containers, list order,
dictionary insertion order and escaped valid JSON Pointers. **3/3 strictness
controls fail**: `pointer()` accepts `/-1`, `/01` and `/+1` as list indexes. Python
`int()` coercion is more permissive than canonical JSON Pointer array indexing.
These are scorer weaknesses, not evidence that native provenance emitted invalid
pointers. The earlier owner-shard pointer-equality counts remain resolution
measurements, not strict JSON Pointer validity certification.

Reproduce against the current runner from repository root:

```bash
python3 docs/research/w6_evidence_2026-09-30/transport_review_real_export.py \
  loom/tools/w6_evidence/real_export_audit.py
```

First controls are preserved in `transport_review_real_export.json`. The parent
subsequently fixed canonical array indexing and malformed escape handling in the
active runner, preserving the original owner-shard instrument. Re-run controls
are in `transport_review_real_export_after.json`: **13/13 pass**, including two
additional checks that a preexisting output directory is refused before runtime
creation and its first-evidence bytes remain unchanged. No owner data was opened
or reimported for these controls.

The tool
does not reject duplicate JSON object keys at parsing, so reconstruction claims
cover parsed JSON and independent source-byte retention, not duplicate-key
semantic preservation. Nonzero importer return codes are recorded rather than
automatically converted into a passing test exit; readers must evaluate the
saved metrics. No production change was made by this reviewer.
