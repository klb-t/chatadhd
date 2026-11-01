# XLSX basic

Local Python standard-library reader. From the repository root:

```sh
python3 -B -m loom.tools.xlsx_basic --sheet 'Zażółć' --range A1:C3 < loom/tests/xlsx_basic/table.xlsx
python3 -B -m unittest discover -s loom/tests/xlsx_basic -v
```

API: `read(bytes)`, `read_cell(bytes, sheet_name, 'A1')` and
`read_range(bytes, sheet_name, 'A1:B3')`. All accept `limits` and
`expected_sha256`. Sheet names are exact, grid coordinates are 1-based A1
(absolute `$A$1` accepted), and ranges are inclusive. A sheet must be named
when selecting a range. The workbook is fully parsed within its budget even
when selecting a cell; this is not a seekable XLSX implementation.

`preset.json` holds configurable ceilings; zero disables each `max_*` ceiling.
`read_chunk_bytes` must remain positive. The optional CLI `--limits-json`
accepts a complete trusted limits object. Defaults bound input, actual inflated
bytes, XML nodes/depth, cells, selection expansion and serialized output.
Deployers must also bound process time/memory and transport output. The module
does not install dependencies, spawn a child, access a network or extract ZIP
members to disk. The CLI consumes stdin and writes one JSON object to stdout.

Supported: OPC root/workbook relationships, multiple sheets (including hidden),
transitional/strict SpreadsheetML namespaces, shared strings including rich
runs, inline strings, lexical numeric/date values, booleans and explicit empty
cells. Worksheet dimension/count metadata never drives allocation. Missing
cells have `status=absent`, distinct from observed blank cells.

Every `<f>` cell, including shared/array formulas without formula text, has
`status=unsupported`, `code=xlsx_formula_unsupported`, `value=null`, `text=null`.
Cached values are never promoted into observations. Unknown/error cell kinds
are also explicit unsupported cells. This reader applies no styles, date serial
conversions, merged-cell expansion, charts, comments or recalculation. Results
are always marked partial, with warnings; source bytes remain authoritative.
It refuses external relationships, encrypted ZIPs, macros/active content types,
symlinks, unsafe/duplicate ZIP paths, unsupported compression, ZIP64, malformed
structures, DTD/entities and resource overruns. This deliberately small subset
does not claim to read every valid Excel workbook.

`container.py` adapts the accepted DOCX reader's bounded ZIP/XML operations
(base e2f6873c); it has no runtime dependency on the DOCX package.

`import_hook.register_with_resource_graph()` uses the existing parser registry,
without replacing handlers. Explicit `format='xlsx'` is required; no heuristic
or suffix registration is introduced. Parser options `xlsx_sheet`, `xlsx_range`,
`xlsx_limits` and `xlsx_original_sha256` flow to the same reader.

`prepare_source_parts()` is a pure bridge for the native Source/Unit/Observation
pipeline. It verifies the expected original hash and returns separate original
and derived-text descriptors, cell Unit/field Observation drafts and exact UTF-8
byte spans. Original SHA/sheet/member/range/XML path remain in `attrs.xlsx`.
Those paths address original XML, not bytes in the ZIP. Native byte locators
address only the derived text hash. Unsupported/absent cells remain explicitly
in `unavailable_cells`, without a fabricated Observation or genesis text.
The bridge assigns no persistent IDs and writes no graph. The native caller
must use existing model ID factories and shared genesis in its transaction;
the plan is not a persistence/materialization receipt. Changing selection,
original or method must change native projection identity even if text repeats.
