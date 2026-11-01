# DOCX → existing source parts contract

The existing `client.blob_text` action / `POST /api/client/blob/text` accepts
`format: "docx"`. Upload alone never invokes it. Example after the ordinary
multipart upload or `client.blob_begin/append/commit`:

```json
{"hash":"<original DOCX sha256>","source_key":"document:stable-caller-key",
 "name":"notes.docx","format":"docx","language":"pl"}
```

`previous_source_id`, when supplied, names the previous **derived** Source returned
by this action under the same `source_key`. Historical sources remain readable;
there is no mutable latest-source pointer or CAS. Supplying a foreign predecessor,
a raw DOCX Source instead of a derived one, or a changed predecessor on an existing
receipt is refused. The ordinary blob raw endpoint still serves both originals
and derived artifacts by their respective hashes.

## Representation and provenance

- The configured `.docx` entry in the trusted `chat_context` runtime profile selects
  the existing `docx_basic` reader. Its ZIP/XML safety checks, limits, timeout and
  output cap remain in force. Request JSON cannot supply an executable or a reader
  result. A missing reader or old helper without projection ranges is an error.
- `src_docx_<original hash>` reuses the existing raw DOCX Source registration.
  Its graph node carries a whole-file Unit, without fictional page or XML byte
  offsets. The original ZIP is never overwritten.
- The reader's exact UTF-8 rendering has a separate blob hash and Source. Its
  identity includes caller key, text hash, original identity, extraction receipt
  hash and transformation identity. Two ZIPs yielding identical text still have
  different derived Sources and occurrence nodes; their text blob may be shared.
- The transformation records `loom.docx_basic/1`,
  `loom.docx_basic.projection/1`, the actual saved reader script's SHA256 and the
  effective reader limits. Full extraction JSON (including warnings, blocks and
  ranges), script, text rules and knowledge-pack rules are content-addressed blobs.
  Paths to executables are not document metadata. Python/system dependency binaries
  are not bundled into provenance; the saved extraction is the exact replay input.
- Derived Source → raw Source and method are ordinary graph links. A Source
  provenance record ties the derived Source to the original and transform hash.
  Each selectable occurrence has its own Unit, provenance record and link to the
  existing genesis version/fingerprint. Equal text is shared only as a fingerprint,
  never as occurrence identity.
- `source_sha256`, Unit/Observation locators and `byte_range` in the result address
  the **derived text**, marked `coordinate: "derived_utf8_bytes"`.
  `derivation.original_sha256` addresses the ZIP. Reader locators such as
  `word/document.xml#body/2` mean the zero-based body child recognized by this reader,
  not an XPath, page coordinate, XML byte range or JSON pointer.

The default `recognized_block` cut uses reader boundaries (including paragraph
internal line breaks). Each block owns its following generated newline; heading
and list prefixes are rendered as before. Empty blocks, including a trailing empty
block, keep separate occurrences. `text_range` excludes generated prefixes and
separators: Observations quote only recognized block values in the derived text.
Heading/list kinds map to existing Observation kinds; whole paragraphs and tables
use `field`, with `recognized_kind` and `evidence_class: "derived"` recorded
explicitly. No new closed-set enum, AST, inferred chapter, table-cell graph or
page/layout claim is introduced. Warnings and `status: "partial"` are retained.

`level` may instead request an existing text Segmenter level (e.g. `word`), making
a finer projection of the same derived Source. It does not claim additional XML
structure. The existing `genesis.record_initial_additions` policy also applies
to the recognized-block cut, using the existing event-kind mapping and silent
operations. Unit kind/observation mappings, graph relation names and the default
cut are pack data. Code enforces source/hash agreement, valid UTF-8, range tiling,
recognized locator order, extraction version, provenance identity and coordinate
separation; an overlay cannot redefine these invariants.

## Reads, selection and persistence

`client.blob_text_read {"node_id":"<occurrence>"}` checks both the derived blob
and original DOCX blob, the saved extraction, range and text hash. It does not need
to execute the reader again. Materialization recomputes the reader output and
checks its input hash; a changed script during execution or stale/malformed output
is refused. A cached DOCX projection requires its original, derived text,
extraction and script blobs to remain available and valid. As in G1, it is a
receipt, not repair or a full graph-integrity audit.

The existing G1 unified selector consumes these occurrence IDs via
`context_execution.unified.legacy_seed_ids` with normal budget/hop settings.
Its existing database-only validator checks the derived Source and original
Source descriptor as well as occurrence text and coordinates. It does **not**
read BlobStore files during selection; disk integrity is checked by the explicit
read/materialization paths. This change does not claim a new UI entrypoint or
execution of the chat/UI acceptance gates.

Writes use the existing materializer transaction, nodes/links and provenance
stores; no SQL migration or public C ABI addition is needed. Failed transactions
may leave content-addressed files on disk, as with existing BlobStore writes;
they do not replace original bytes or commit a successful graph receipt.
Existing text occurrence records remain readable. Adding the DOCX recipe changes
the pack hash, so new method projections of old text Sources may receive new
method IDs. An older binary's selector does not understand the new derived
coordinate value; do not claim rollback acceptance for these new occurrences.
