# W6 independent native import audit

Baseline **b118c80e981c08ec6d7f9ab6aacc177979186cf2**, synthetic-only evidence.
The first frozen audit produced **47/59 passing named checks**. This is a
heterogeneous diagnostic denominator, not a quality score or real-export
validation. All first results, including overly strict direct-blob checks,
remain unchanged in `import_first_run/`.

## Results

| Dimension | Observed result | Meaning |
|---|---|---|
| Complete original source bytes | 3/3 inputs preserved byte-for-byte | Both ZIPs and the wrapped JSON survive in BlobStore. |
| ZIP member recovery | 7/7 members, 7,628 bytes recovered exactly from stored ZIPs | No observed member-byte loss. Two first checks required separate member blobs; their failures do **not** mean irrecoverability. |
| Raw message objects | 9/9 per-case messages equal their originals | Includes three repeated OpenAI messages in the wrapped-input case; only six unique synthetic messages. |
| OpenAI structured reconstruction | 54/54 scalars + 14/14 empty containers | Exact JSON paths, types and values; object-key order ignored. |
| Anthropic structured reconstruction | 63/73 scalars + 20/20 empty containers | DFS storage changes original `chat_messages` array order. Ten scalar path/value pairs differ. |
| Wrapped OpenAI conversation reconstruction | 54/54 scalars + 14/14 empty containers | Conversation reconstructs; unknown wrapper field is absent from structured metadata. |
| Branch status and parent links | 9/9 statuses; 6/6 parent checks | Current/rejected branches remain distinguishable in these fixtures. |
| Resolved binary attachment | 1/1 readable blob matches 260 input bytes | Attachment metadata is also included in raw-message equality. |
| Exact provenance-only source locators | 1/9 resolves to the correct original message | Two Anthropic ordinals swap parent/child. OpenAI locators omit the mapping key/JSON Pointer. |

Leaf counts above treat empty containers separately. Importer counters count
empty containers as leaves. For Anthropic the importer reports **93/93**
(73 scalars + 20 empty containers) with `partial=false` and no errors. That
counter establishes neither original array order nor exact source locators.

## Reproductions for file owners

1. **Anthropic original array positions are not retained in structured rows.**
   Fixture order: `child/~current`, `parent`, `child/rejected`. Persisted row
   order: `parent`, `child/~current`, `child/rejected`. `metadata.export.raw`
   preserves each object, but no original array index accompanies it.
   `loom/src/import/export_anthropic.cpp` constructs DFS `order`, inserts in
   that order and stores `raw` without the original `i`. The complete source
   ZIP remains recoverable, so this is a structured-representation boundary,
   not destruction of original data.
2. **Provenance locators use result enumeration, not exact source location.**
   `loom/src/import/importer_core.cpp::record_provenance` writes sequential
   `message_index`. For Anthropic this gives parent index 0 and child index 1,
   opposite to the input. OpenAI mapping objects do not have array ordinals;
   their provenance locator lacks a node key or JSON Pointer. OpenAI message
   `metadata.export.key` separately retains the key, so a caller can assemble
   an exact locator by joining extra metadata. The provenance record alone is
   insufficient. This audit does not claim all possible metadata joins fail.
3. **Unknown wrapper field has no structured representation.**
   `wrapped.json` contains `unknown_wrapper` next to `conversations`.
   Conversation/message/node metadata lacks that key. Original file bytes
   survive, making reconstruction possible by rereading the source. Reporting
   only conversation leaf counts does not cover the wrapper.

These are receipts for the owning lanes. Production code and existing tests
were not changed. No holdout or owner export was opened; no provider called.

## Reproduce

From repository root, the checked-in fixtures and instrument are already frozen:

```bash
python3 loom/tools/w6_evidence/import_audit.py evaluate \
  --binary /absolute/path/to/baseline/cli/loom \
  --out /absolute/path/to/new-empty-result-directory
```

The command refuses changed protocol/instrument/fixtures and an existing
output directory. It saves native stdout/stderr, SQLite observations and the
receipt. Named failures are reported in JSON; exit 0 means the evidence run
completed, **not** that every check passed. The importer processes are closed
before SQLite is opened read-only. No reopen/reindex semantic claim is made.

Supplementary ZIP-byte recovery is reproducible without trusting counters:

```python
import io, pathlib, zipfile
fixtures = pathlib.Path('docs/research/w6_evidence_2026-09-30/import_fixtures')
results = pathlib.Path('/absolute/path/to/new-empty-result-directory')
for case in ('openai', 'anthropic'):
    expected = (fixtures / (case + '.zip')).read_bytes()
    stored = next(p for p in (results / case / 'blobs').rglob('*')
                  if p.is_file() and p.read_bytes() == expected)
    with zipfile.ZipFile(io.BytesIO(expected)) as a, zipfile.ZipFile(stored) as b:
        for name in a.namelist():
            assert a.read(name) == b.read(name), (case, name)
```

## Evidence identity and limits

- Protocol/fixture/instrument manifest SHA-256:
  `c45ea8da69422985a99c3c75981756809889cd50b5fd7d7d35fd047d00847337`.
- Native CLI SHA-256:
  `c08e869e687a757ba05ca6fd80e51e16d8d3282ba575294e4c5fb34ee0ce52fc`.
- Actual command path:
  `/workspace/scratch/a371a1ca13b1/verification/native-dev/cli/loom`.
- CMake home points to `/workspace/scratch/a371a1ca13b1/chatadhd/loom`;
  that source tree has clean Git status and HEAD equal to the stated baseline.
  All import `.cpp` files byte-match this audit checkout. Per-file SHA-256 and
  dynamic dependency listing are in `import_supplement.json`. This is observed
  build-directory/source evidence, not a reproducible-build attestation.
- `import_protocol.md` and manifest were written before the first native run.
  Fixture JSON/ZIP files were authored independently; no production fixture
  generator or EXPECTED oracle was imported. Storage schema and source code
  were inspected, so this is independent authorship, not blinded assessment.
- Do not combine these diagnostic checks with existing tests or claim this
  validates arbitrary real exports, multigigabyte archives, Android, or model
  comprehension. This does not evaluate malformed JSON/UTF-8 recovery.
