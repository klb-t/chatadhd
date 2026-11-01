# CSV recognized parts

Executable local adapter, Python 3.10+ stdlib only. It reads bytes and returns
JSON; it does not modify input files, create a database, contact a provider, or
materialize a second graph/genesis engine. This increment is an independent
reader. Native materialize/read/context wiring belongs to the sole integrator.

```sh
python3 -B loom/tools/csv_structure/reader.py \
  loom/tests/csv_structure/fixtures/polish.csv \
  --source-key fixture:csv/pl --delimiter ';' --header --language pl
python3 -B -m unittest discover -s loom/tests/csv_structure -p 'test_*.py' -v
```

The CLI emits a single JSON result, or exit 2 with a content-free JSON error on
stderr and no partial result. `--selection selector.json` replays a selection
against the original file. `--max-output-bytes` has a 32 MiB preset, including the
newline; 0 disables it. This run uses only tiny synthetic fixtures. There is no
implicit field-size limit inherited from `csv.reader`. The reader loads input
and output in memory; the integrator must use its ordinary configured process,
memory, time and input/output budgets when invoking it.

## Parsing and evidence contract

`Options(delimiter=',', quotechar='"', header=False, language='')` are explicit
presets. Delimiter and quote are distinct single Unicode characters other than
CR/LF. Quoting is strict: a quoted token starts at the beginning of a field;
doubled quote characters escape a quote; only a delimiter, record ending or EOF
can follow its closing quote. Spaces are retained. CRLF, LF and CR record
endings and quoted multiline fields retain their original bytes and line
endings. UTF-8 is validated, never repaired; a leading UTF-8 BOM is syntax and
remains part of the original hash/blob. No dialect/header/type inference occurs.

An empty file (or BOM alone) has no records. A blank physical record has zero
cells; `""` has one quoted empty cell; `,` has two empty cells. A terminal record
ending does not invent another row. Ragged widths are reported in a warning,
not padded, dropped or rejected. Header selection retains row 1 in all ordinal
coordinates; duplicate/empty labels remain distinct ordinal columns. No formula
evaluation, type coercion, normalization, repair, confidence score or XLSX reader
is provided.

`loom.csv_structure.result/1` includes:

- `source`: the common `SourceRecord` JSON shape. `blob_hash` and
  `original_sha256` hash **all original bytes**, including BOM/record endings.
  Identity is `src_text_ + SHA256(canonical([source_key, original_sha256]))`, as
  in native text source parts. A caller key establishes identity; a filename
  is a display title. The caller retains the original blob; this reader does
  not replace it with parsed JSON or decoded cell values.
- `units`: common `model::Unit` JSON, one `csv_row` per logical record, with
  original-byte `Locator` and physical line interval. Row quotes exclude their
  record ending. `record_end` in `rows` includes the original ending so cuts
  plus BOM and delimiters can reconstruct the full original.
- `observations`: common `model::Observation` JSON, `kind=field`, one per cell,
  linked to its row unit. `text` is exactly the original UTF-8 token, including
  quoting/escaping. Its `Locator` has original byte offsets, not code points;
  `line` is the 1-based physical starting line (display only). Line intervals
  count CRLF once. `attrs.decoded.value` is explicitly **derived**, not a quote
  with a raw-source offset. Empty tokens have valid zero-byte spans.
- `rows` and `columns`: recognized structural references, using row/column
  ordinals starting at 1. Columns reference cell observations, not a fabricated
  contiguous original-byte span. Unit/Observation IDs follow native
  `make_id`/`kb::stable_id` and canonical JSON conventions. Projection-specific
  attrs and language must stay in the method receipt/occurrence metadata,
  as native source parts already does; do not globally overwrite an observation
  keyed solely by the location/text ID when a dialect changes.
- `method`: parser ID/version, exact reader source SHA, encoding and all explicit
  options. The hash binds selectors to a reproducible interpretation; retain
  this reader's bytes and output receipt alongside the original.
- `genesis_input`: original-byte row cuts with raw text and ordinal. These are
  inputs for the **existing** Segmenter/genesis operations. No normalized
  fingerprint, cross-version alignment or genesis event is fabricated here.

`read_selection` reparses original bytes using the supplied options. It checks
the selector's original SHA and method SHA before using coordinates. It accepts
`kind=row|cell|column|range`, inclusive `rows=[first,last]` and (except a row)
`columns=[first,last]`. A `cell` is one coordinate, a `row` is one row, a `column`
spans all rows of the source, and a `range` is a rectangle. Out-of-bounds and
malformed coordinates fail. An absent ragged cell fails for a cell read; column
and range reads return explicit `missing` coordinates along with existing cells.

Row reads return the raw record quote/locator. Other reads return individually
located observations, preserving discontiguous selections; they do not join a
range and claim it is one original quote. The returned `csv:sha256:...#cell=...`
or `#row=...` citation is an application identifier bound to original and
method hashes, **not** a claim of RFC 7111 conformance. Persist the structured
selector too. The original common Locator remains the byte-level evidence.

## Trusted native host — 2026-11-01

The standalone author delta was received once; its 15 tests were not rerun merely
for integration. The native host now generates this receipt from immutable blob
bytes using the installed pinned reader. Existing `client.blob_text` accepts
`hash`, `source_key`, `name`, `format="csv"` and optional `previous_source_id`.
CSV options come from the installed pinned profile; request `pack`, `options`,
paths or receipts are refused. There is no dialect sniffing or client trust flag.

Rows and cells become separate raw-byte occurrences through the existing
Source/Unit/Observation/genesis machinery. A row and its sole cell may have the
same location-derived Unit ID but have distinct role-occurrence IDs, bound to
method and ordinal. Decoded values stay derived attrs, never replacement source
quotes. Empty/missing cells and physical lines keep the standalone contract.

`client.blob_text_read` takes `node_id` and optionally `csv_selection` using the
selector from the unit attrs. Row/cell/column/range selection is reparsed by the
host. Ragged missing cells remain explicit. Existing account-global graph seeds
select occurrence nodes for context; no CSV-specific UI is claimed here.

See `loom/tools/structure_readers/README.md` and the scoped native/HTTP tests
under `loom/tests/structure_readers` / `loom/server/tests/test_server_host_readers.py`.
Native read and context replay deliberately refuse when the pinned installation
changes. Retain the matching package for old method projections; migration and
UI selection across parser versions are not part of this increment.
