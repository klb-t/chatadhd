# N4 — import source materialization and bounded root dispatch

Base: `926072c`, branch `gpt/night2-import-2026-10-01`. ROOT approved the private importer helpers, additive metadata and parser version `export-3`; no table/schema or public C ABI addition. Changes are importer code, one dedicated synthetic suite, the existing fidelity suite's parser-version assertion, and this receipt/evidence. No frozen W6 oracle, private corpus, paid model or sealed holdout was used.

## Source mapping

| Existing record | Additive behavior |
| --- | --- |
| Whole archive `loom_sources` row and immutable BlobStore bytes | Retained. Its observed outcome is `pending`, `complete`, `partial`, `cancelled` or `failed` in metadata. No historical row is relabelled. |
| Every successfully extracted safe, non-directory ZIP entry | Stored by `BlobStore::put_file`, which streams hashing/copying. Existing `SourceRecord(kind=zip_member)` stores the exact member blob hash and size, `parent_source_id`, and locator `{source: sha256:<containing archive>, member: <original entry name>, archive_index: <central-directory position>}`. |
| Source provenance | Existing `ProvenanceRecord(subject_kind=source)` links the member source to the containing source with the same locator and `extract.zip@export-3`. No new source ontology. |
| Existing conversation/message provenance | Still addresses its source archive/member/JSON pointer. `archive_index` additionally distinguishes repeated member names. Nested parsing-source metadata identifies its containing archive/member/index; extracted child-member sources link to that inner parsing source. |
| Existing export report | Each processed/materialized member carries `source_id`, `blob_hash`, locator and index; explicit extraction/materialization errors and nested partial outcomes prevent a complete outcome. Extracted entries not interpreted after cancellation remain reported. |

Per-index temporary extraction paths prevent one entry from overwriting another with the same name. Original member spelling, including repeated separators, remains in locators. Both conversations in repeated conversation-file names are parsed in stable archive order. Duplicate asset paths have no unique binding: the existing resolver returns unresolved and the report records `ambiguous_asset_member` / partial; it does not pick a last or first file. All extracted duplicate bytes remain separate content-addressed blobs (identical bytes naturally share one blob).

Both provider-export and legacy ZIP extraction materialize member sources when `record_provenance` and the stores are available. Existing whole-archive bytes also retain entries that cannot be separately extracted. Unsafe paths remain rejected; corrupt/stat-failed entries are reported. There is no new size/count cap. Existing depth, inline display and generic-inference limits remain.

## Prefix and memory boundary

The loader discards arbitrary leading JSON whitespace in fixed 64 KiB chunks, recognizes a BOM even when it crosses a chunk boundary, and then always uses the normal array/object/wrapper dispatch. It no longer reads an entire large array after a whitespace-only first chunk. Cancellation is checked during scanning and array streaming. UTF-8 repair, malformed-element diagnostics and early callback stop remain explicit.

This does **not** make every JSON shape constant-memory: top-level objects and wrappers retain the existing whole-document DOM path so all wrapper sibling fields survive; individual array elements still require their own buffer/DOM. The legacy tolerant array splitter/error policy is otherwise retained. This is not a new strict JSON parser or a fidelity claim for arbitrary duplicate JSON keys.

## Cache and retry boundary

`export-3` is distinct from `export-2` and older representations. A completed source stores its resulting conversation IDs and report. Only a recorded `complete` outcome with intact referenced conversation rows can be reused, including nested and zero-conversation archives. Cached results return the original source ID/report. Raw-member sources cannot masquerade as parsed-source cache entries because parser identity is checked.

A cancelled, failed or partial import is not reused as complete. Retry creates a new representation; conversations already committed by an earlier partial run **remain and can therefore coexist as duplicates** with successful retry rows. This preserves evidence instead of silently deleting or merging it. `force` still permits explicit reimport. Legacy non-export parser identity and its existing failure policy remain unchanged. A completion marker records the observed import outcome; it is not a new transactional snapshot of every external file or a periodic blob integrity audit.

## Verification and peer review

Eight new authored synthetic native cases cover 30 whitespace/BOM × root-shape combinations around 64 KiB, non-object array prefixes, UTF-8/chunk handling, invalid/truncated JSON and cancellation; exact bytes and locators for JSON/viewer/unknown/binary files; duplicate names and asset ambiguity; nested ancestry; zero-conversation cache; cancellation during extraction and after a committed conversation; malformed member/retry; and export-2/legacy boundaries.

First syntax-only pass: all six affected implementation/test translation units passed with the shared compile database's C++20/Werror flags. Commands and output are retained in `syntax-first.log`. This does not link or execute the tests.

Independent read-only peer review (`design_audit`) found one concrete cache blocker: the inherited bare-export callback discarded malformed elements encountered before provider recognition. Retaining those local errors now marks a later recognized provider import partial; a non-provider fallback still discards the unused export report. A synthetic `[{bad}, <valid Anthropic conversation>]` regression checks error index 0, partial status, explicit retry and retention of the valid prior row. Re-review found that blocker resolved and no further blocker in the reviewed scanner/BOM, entry identity, ancestry or cache/cancel paths. The changed archive/test units passed a second syntax-only check (`syntax-review-fix.log`). Original logs were not overwritten.

Native execution remains **pending the single coordinated verification build**. Frozen W6 V2 and the original V1 member-blob checks must run unchanged against that final binary; no expected result here is reported as measured. No independent C++ build was started in this lane.
