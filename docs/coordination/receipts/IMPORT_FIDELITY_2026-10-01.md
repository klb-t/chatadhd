# Native export fidelity correction — implementation freeze

Base: `96ea1c727ee5bbba4ee056dfa15aa844d1102d22` (integrated W1–W6).
Implementation: `0f29a689d1af06115ba16dff69f8bf877226122a`.
Nested-archive locator follow-up: `2bddeb15788e8696cdeb851b6ffc1cd39aea98c4`.
Branch: `gpt/fix-export-fidelity-2026-10-01`.
Scope: importer implementation, approved parser-version constant, independent
`loom/tests/test_import_export_fidelity.cpp`. No schema/ABI change, model call,
private archive access, sealed-data access, or frozen W6 modification.

## What changed

- Anthropic messages are inserted in their original `chat_messages` array
  order. `source_index` retains that address; `traversal_index` retains the
  separate graph traversal position. Existing branch/status inference stays
  separate. Message IDs are allocated before insertion, so a child can retain
  its native `parent_id` even when the parent appears later in the source.
  The existing conversation transaction commits all rows together.
- Source locators now carry an RFC 6901 `json_pointer` (`json_path` remains an
  alias for existing consumers), source SHA-256, local conversation position,
  and ZIP member where applicable. OpenAI mapping keys are escaped explicitly;
  object keys are not presented as array offsets. Anthropic `message_index`
  now means source array position; `view_message_index` labels the separate
  time-sorted database view. Legacy global `conversation_index` remains.
  For nested ZIPs, `source` identifies the innermost source blob and `member`
  names an entry local to that blob. Legacy `zip_member` retains its contextual
  containing-archive chain; it is not reused as an entry name in another blob.
- Conversation metadata records the source container and local position.
  Every sibling field of a recognized `conversations` wrapper is retained in
  `export.wrapper_fields`, including unknown, null and empty values. Raw input
  bytes remain in the existing content-addressed BlobStore.

The new metadata is additive under the existing `export` object. No separate
source ontology or record kind was introduced. A source pointer addresses
the parsed source JSON member/file, not a reconstructed conversation with a
different root shape. It is not a claim about duplicate-key JSON, exact textual
formatting, or separate member-blob availability. Original bytes preserve the
textual representation.

## Parser/cache and reimport boundary

The coordinator explicitly approved `kExportParserVersion = "export-2"`.
Export source deduplication already includes parser version, so importing an
`export-1` source again creates an `export-2` representation; old sources,
conversations, provenance and blobs are retained. Subsequent identical
`export-2` imports reuse that representation. Existing rows are not silently
rewritten or migrated. Users who need corrected structure must reimport the
original source through the export path (bare JSON uses `--export-mode on`; ZIP
uses the existing export default). Legacy non-export parser version stays `1`.

## Validation boundary at freeze

Completed locally: `git diff --check`; W6 fixture decoder verified both frozen
ZIP hashes against their original manifest. Decoded ZIP files are generated
local inputs, not committed changes to the frozen dataset.

Native build/tests are **pending the single shared verification build**;
no old binary result is claimed for this implementation. New independent
suite `import_export_fidelity` has five test cases covering:

1. Source array reconstruction, forward parent relationships, distinct DFS
   positions, branch statuses and original bytes.
2. `export-1` to `export-2` reimport without deleting old data, followed by
   current-version deduplication.
3. Six provider × root-shape combinations (Anthropic/OpenAI; object/array/
   wrapper), actual-root pointer resolution, escaped keys, unknown wrapper
   fields and source bytes.
4. Two ZIP shards containing three conversations: member-local indices reset
   independently of global import order; locators resolve in each member;
   original ZIP bytes survive.
5. A ZIP containing another ZIP: the conversation's actual source record
   binds the inner archive bytes; local member and JSON pointer resolve in
   that source; the contextual legacy chain and outer archive bytes survive.

Independent read-only review by `design_audit` at implementation `0f29a68`
found no blocker; no build or native test was performed during that review.
It confirmed source-array coverage, forward-parent index alignment, escaped
mapping-key locators and callback initialization order. The inherited loader
fallback after more than 64 KiB of leading whitespace remains outside this
change; it bypasses normal root dispatch and is not covered by these claims.

Frozen W6 V2 must be rerun unchanged into a new output directory after the
shared build. Preserve the first baseline **23/24 V2** and **47/59 V1**;
these overlapping denominators must not be added. V2 tests recursive
structural equality and source array order. V1's wrapped OpenAI locator check
resolves pointers against the unwrapped `openai.json` array even when the
imported source root is the wrapper object. Correct `/conversations/0/...`
locators can therefore remain negative in that frozen check. The new native
test independently resolves against the actual wrapper root. We do not modify
that old instrument, erase its failures, or report the whole V1 arm green.
V1 separate-member-blob checks also remain outside this correction; original
ZIP byte preservation is a different claim.

Implementation test-file SHA-256:
`79dd9144712e035b28b2aabf376c7c594f1ccdfb93506cb1e48329852785f482`
(at nested-archive follow-up).
Fresh shared-build results and source/binary hashes belong in the verifier's
new receipt; this freeze receipt does not substitute for them.
