# ODT basic reader

Local stdlib-only ODT ZIP/XML reader, plus a pure adapter to Loom's existing
`SourceRecord`, `model::Unit`, `model::Observation` and `text::genesis::SourceRef`
JSON contracts. No installation, database writes, models, network, resource
fetching, subprocesses inside the reader, or source-file changes.

From the repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s loom/tests/odt_basic -p 'test_*.py' -v
python3 -B -m loom.tools.odt_basic /path/to/file.odt "$(cat loom/tools/odt_basic/limits.json)"
```

The CLI also accepts `-` for binary stdin. Its optional third argument is a JSON
object with `adapter` (the object in `adapter.json`), `source_key`, `name`, and
optional `language`. This returns an import plan instead of extraction alone.
Success exits 0 with one UTF-8 JSON document; failure exits 2 with an error code
and no source text or traceback. File paths, interpreter, module/package location,
timeout and limits must come from trusted application configuration.

## Coordinates, evidence and identity

- `source.sha256` hashes **all original ODT bytes**, not XML or decoded text.
  Each `raw_locator` binds this original to `member=content.xml`, its own SHA256,
  and `xml_path`. The path is zero-based element-child indices from the XML root
  (`[]`); text, comments and processing instructions do not count as children.
  Namespace-expanded element/attribute names are retained in `observed`.
- `projection` is a separate UTF-8 text blob. `blocks[].byte_range` is a half-open
  byte range **only in that derived blob**. There are no invented raw ZIP/XML
  byte offsets, layout coordinates, OCR or semantic claims.
- Headings, paragraphs, tables, rows and cells appear in XML preorder with
  parent indices. Parent ranges include descendant text and table separators.
  Cell and paragraph ranges can coincide. Text blocks are separated by newline,
  table cells by tab, table rows by newline; heading markers are not invented.
- XML element kinds and explicit attributes are observed. Decoding, inline
  whitespace expansion and the text projection are derived. `evidence.inferred`
  is empty. There are no confidence scores, inferred headings/sections, formula
  evaluation, list numbering, page rendering or silent flattening of unknown XML.
  Notes, fields and unknown inline elements are omitted with warnings. Visible
  hyperlink text is retained, but targets are never followed. XML newline/entity
  normalization means decoded text is not a byte quotation from raw XML.
- Repeated rows/cells are represented **once per XML element**, with explicit
  `repeat`, `logical_row` / `logical_column` and a warning. Covered cells stay
  empty, and span/merge/value attributes remain observed XML metadata. The logical
  cell budget includes repetition products. Consumers must not label compact
  output as an expanded spreadsheet or a reconstruction of visual layout.
- Import preparation returns three sources: original ODT, derived UTF-8, and
  canonical extraction JSON. Units use RFC6901 `/blocks/N` locators in the saved
  JSON source; Observations quote byte ranges in the UTF-8 source. This avoids
  collisions between a table, row, cell and paragraph with the same text range.
  `attrs.original_locator` links back to original ODT/XML, `parent_unit` describes
  containment, and `text_locator` links a Unit to its projected text.
- Unit and Observation IDs follow the existing `join_key`, `json::canonical`
  and 16-hex `kb::stable_id` algorithms. A different raw ZIP (even with identical
  XML/text) yields a different source/version/occurrence identity. Genesis gets
  a `SourceRef` and the full projection for the existing segmentation pipeline;
  overlapping structural ranges must not be passed as disjoint genesis cuts.

`adapter.json` contains replaceable source/unit/observation kinds. A paragraph
uses the existing `sentence` observation enum by preset; it is not sentence
segmentation, and `recognized_kind=paragraph` preserves that distinction.
`prepare_import` validates before returning any inputs to a writer. `read_unit`
replays the reader against original bytes and compares the complete saved plan
before returning one quote; stale originals and altered receipts are refused.

## Limits and supported container subset

`limits.json` is a replaceable caller preset, not a product policy. Every ceiling
is explicit; 0 disables a ceiling. `read_chunk_bytes` must be positive. Unknown
or mistyped settings are refused. XML depth/node, archive count/size/ratio,
logical table cell, unit count, projected byte and serialized output budgets
produce distinct error codes. Output is measured incrementally, including the
larger import plan. Even with ceilings disabled the local Python runtime can
report `odt_parser_depth_unsupported`; disabling budgets is not a promise of
unbounded runtime resources.

ZIP member paths and decoded aliases are validated for every member, including
unused members. Duplicate/aliased names, traversal, absolute names, symlinks,
encrypted streams, unsupported compression, ZIP64, split/prepended/appended
containers and corrupt/overlapping streams are refused. Stored/deflated streams
are validated through their actual end with bounded zlib output, size and CRC
checks: a forged central size plus a prefix CRC cannot conceal a bomb. The actual
central directory is counted before allocating ZipInfo objects. Auxiliary member
streams are checked but not retained; auxiliary XML is not parsed or fetched.

`mimetype` must equal the ODT MIME and `content.xml` must contain one
`office:document-content/office:body/office:text`. The reader is a bounded content
reader, not a full ODF schema/manifest/signature validator. DTD/entity declarations
and external entities are forbidden by Expat callbacks, including UTF-16 input;
predefined XML entities and numeric character references are allowed. No XInclude
or stylesheet engine is invoked. Raw originals remain the caller's responsibility
to store verbatim in the existing immutable BlobStore.

## Sole-integrator handoff

The module is executable now; native import/HTTP/UI registration is intentionally
not applied by this author. Only integrator
`01a192f5-2814-7e52-bf0c-32f3428151ed` performs shared integration:

1. Verify delivery manifest/patch SHA and each allowlisted file, then
   `git apply --check product.patch` on the next base. Apply only after inspecting
   any concurrent additions. Optional `integration.patch` adds just the focused
   CTest entry; it does not register a runtime reader. Run the stdlib command above.
2. Provide a trusted local Python/package invocation using the existing process
   adapter mechanism (`src/chat/docx_basic/adapter.cpp`, `package.cpp` are references).
   Use argv without a shell, pin installed helper bytes/configuration, and enforce
   wall timeout/stdout cap. The ODT packet differs from DOCX: do not route it through
   the DOCX validator or manufacture `.docx` coordinates. Helpers must not be
   selected by document-supplied attributes.
3. Add an explicit ODT branch in `src/text/source_parts.cpp` (or an extracted shared
   internal document adapter), and a reader entry in `chat_context.pack` only after
   the runtime supports it. Keep the existing DOCX/text behavior and ABI intact.
   Any format/segmentation entries belong to the sole integrator, not this patch.
4. Verify raw hash/size against the admitted BlobStore object. Call
   `prepare_import` with the supplied limits/kind data; independently check returned
   hashes, byte slices and parent links before persistence. Retain canonical output,
   exact helper files, configs and transform hash as replay artifacts. Recompute
   native IDs with `model::Unit::make_id` / `Observation::make_id` and compare.
5. In one existing transaction store original bytes, UTF-8 and canonical extraction
   JSON in BlobStore, register the three `SourceRecord`s in `ProvenanceStore`, then
   use the current knowledge Unit/Observation stores and containment/provenance
   graph operations. Repeated input must be idempotent; different originals stay
   distinct. Do not change schema or substitute projection bytes for the raw ODT.
6. Feed `genesis.source` + `genesis.text` through the existing genesis API with
   resolved segmentation options. The adapter creates no second genesis engine.
   For one-part reads, verify original, extraction/helper/config artifacts and the
   selected locator before quoting UTF-8, as the existing DOCX projection does.
7. Through the sole integrator's runner, perform scoped native/HTTP acceptance:
   upload the deterministic synthetic ODT generated by `package()` in the test;
   materialize 14 structural Units and read exactly the `Żółw` cell; verify all
   three hashes, native IDs and original byte recovery; repeat/reopen without
   duplicate objects; replace only ZIP metadata and retain distinct raw/version
   identity; reject stale hash, altered range/path/evidence and malformed input
   before writes. Use mocks/no provider egress. Run existing text/DOCX sentinels.
   This author's Python results do not establish native, HTTP or UI acceptance.

No `source_parts.cpp`, `unified_context.cpp`, runtime pack, CMake or UI file is
changed by the product patch. Operational checkpoints/results/manifests are not
product files.
