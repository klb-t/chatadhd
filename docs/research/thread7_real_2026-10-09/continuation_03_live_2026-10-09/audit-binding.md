# Connector source/request binding — narrow live-unblock increment

Base C: `cb16b336ee0c59a1ff80b4899dadebce33aa7610`.
Audit A: `9f931da3bb1d1001f9b7d865914ae195d9255b7e`.
No model calls, provider requests, rankings, new real preparations or real queue
jobs were performed. Cost: USD 0. These are tool-boundary checks, not model
quality evidence.

A's unchanged PASS4 connection functions were executed on this C base with
external transport denied. Both A4-C-001 and A4-C-002 reproduced: the two
acceptance cases failed. After the patch all seven integration, contract and
acceptance cases pass. The two inverse assertions that specifically require the
bugs to exist now fail, as expected. They are not counted as product failures or
as additional passing checks. Original and final receipts remain separate.

`connect_prepared.py` identifies its changed producer as
`loom.thread7_prepared_connector/2`. It sorts only the generated conjunction
field names in `$match.fields`; JSON list order, variants, messages and queue
identity guards retain their meaning. Equivalent object-key order now produces
identical operation IDs and idempotent re-add. Deliberately changing variant
list order still changes the spec and is rejected on reuse of an existing queue.

The source fix distinguishes three hash domains:

| Identity | Actual bytes/values hashed |
|---|---|
| Prepared source | canonical complete context-source JSON, indexed by `jobs-index.jsonl` |
| Panel `raw_sha256` / analysis `source_sha256` | canonical native-conversation JSON; **not** ZIP bytes or raw member bytes |
| Normalized source | exact normalized conversation file bytes |

The connector now checks the frozen index and frozen source table; the selected
family and context-source digest; the native content digest against the panel;
and the exact normalized source bytes, native content and family. Thus an equal
family label or declared hash alone does not establish a real-source binding.
Analysis bindings retain both source domains explicitly and mark verified
content. Legacy direct-hash inputs with no context-source table must still match
the declared source hash exactly, but have `source_content_verified=false` and
`legacy_declared_source_hash`; they do not satisfy live real-source readiness.
Request paths must be included in the verified preparation manifest and their
exact bytes still pass the existing queue/payer identity checks.

A read-only check on the already frozen expanded-v2 release verified **12 source
families and 1197 indexed rows**. These rows include dependency/pending rows and
are not 1197 model trials. No full source archive was reparsed and no preparation
was repeated. The source/native hash chain was checked against actual existing
bytes, not merely the simplified audit fixture. The test includes exact
canonical native identity rather than Python's weaker `1 == 1.0` equality.

New tests: **13/13 PASS**. Existing connector tests: **7/7 PASS**. Separate A
connection checks: **7/7 acceptance/contract/integration PASS**. These are distinct
case sets, not one full structure gate. Root publishes the final structure gate.
Full source-free logs and audit receipts are in `audit-binding-evidence.zip`;
private real-source binding details and the previous producer snapshot remain
outside Git. Receipt hashes are in `audit-binding-receipt.json`.

Historical sources, manifests, specs, operation IDs, analysis bindings and
receipts were not rewritten. A v2 generation intentionally yields a new spec and
operation identity. For a new phase use a new scoped plan identity; do not
relabel an old queue or regenerate paid requests under new IDs. The old producer
is reproducible from the pinned base commit and has also been retained privately.
Current newly generated queues remain unreserved and dispatch-disabled until the
existing payer's fresh preflight succeeds. No budget authority was created.

A2-C-003 concerns the separate `PayerBoundary.sync_verified` consumer; this
connector patch does not claim to repair it. The parent task handles that
boundary and public projection A2-C-001/004 independently.

Recheck (from repo root; full stdout must be saved):

```sh
TMPDIR=/var/tmp PYTHONPATH=/tmp/thread7-python-deps:. python3 -m unittest loom.tools.structure.test_prepared_connector_binding_v2 -v
TMPDIR=/var/tmp PYTHONPATH=/tmp/thread7-python-deps:. python3 docs/research/thread7_real_2026-10-09/continuation_01/test_connect_prepared.py -v
```

The audit replay consumes only `connection_tests` from A's pinned public runner;
its selective invocation and source hash are included in the evidence archive.
It does not rerun credential, public-projection or native integration tests.
