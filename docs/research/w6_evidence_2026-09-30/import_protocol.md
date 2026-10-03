# W6 independent import protocol — 2026-09-30

Frozen before the first native invocation. Author: W6 import-audit subagent,
not the production importer author. Baseline requested: b118c80e981c08ec6d7f9ab6aacc177979186cf2.
No existing fixture generator or EXPECTED file is reused. Storage schema and
public CLI documentation are read to locate observations, not to set expected
outcomes from native results. Only new synthetic data; no owner exports,
protected holdouts, network or providers.

## Preregistered expectations

1. OpenAI synthetic export has a null root, a user parent and two assistant
   branches. Current branch is active; the rejected sibling remains version;
   parent links retain the fork. IDs contain `/` and `~`; metadata contains
   empty arrays/objects, null, false, zero, Unicode and embedded NUL.
2. Anthropic synthetic export deliberately lists a child before its parent.
   Native traversal may reorder rows, but all scalar JSON leaves, empty
   containers, and original array positions must be reconstructible from
   structured metadata if the structured representation is claimed lossless.
   Raw source bytes and structured reconstruction are scored separately.
3. Both ZIPs have unknown members; OpenAI has a linked binary attachment and
   an unreferenced binary. Every complete input and every binary/member byte
   sequence must remain retrievable verbatim in BlobStore. Attachment bytes
   and metadata are checked separately.
4. Bare OpenAI wrapper includes an unknown top-level field. Check byte
   preservation separately from whether structured metadata retains the
   wrapper. Absence is a documented boundary, not a silent success.
5. Provenance locators must identify the exact source conversation/message,
   including JSON Pointer escapes and Anthropic array positions. A source ID
   plus traversal ordinal is not automatically an exact source locator.
6. Reopen SQLite read-only after the CLI process exits. Recount primitive
   leaves with type and value; count empty containers separately. Do not use
   export_report counters as the oracle. Compare counters only after the
   independent result exists. Preserve first stdout/stderr and every negative.

## Scoring and interpretation

Each named condition is pass/fail with observed evidence. A primitive leaf is
one JSON scalar (including null); empty containers have a separate count.
JSON object order is irrelevant; JSON array order is relevant. No tolerance
for Unicode normalization or integer/bool equality. Blob hashes are computed
with Python hashlib and confirmed by direct byte comparison. Source bytes
present do not prove indexed semantics, locator precision, real-export scale,
or downstream comprehension. Negative controls deliberately alter one leaf
and one byte, proving scorer sensitivity without changing production data.

Protocol and generated fixture hashes are saved before native evaluation.
Evaluation refuses a changed protocol/fixture against that manifest, refuses
an existing result directory, and creates separate data directories per case.
