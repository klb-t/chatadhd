# Shared installed file-to-graph adapters

This package reuses `chat::read_local_json` and the existing native
`source_parts` materialization/read/context path. It is not a second importer.
Installation-owned `profile.json` selects closed descriptors compiled by
`unknown_structure.contract`; those descriptors map to existing SourceRecord,
Unit, Observation, native ID factories, genesis and provenance. Uploaded files
cannot choose an interpreter, helper, descriptor, limits or a trusted receipt.
The legacy XLS runner is not installed or called by this package.

The host verifies every manifest payload and the interpreter hash, supplies
immutable original bytes through stdin, checks the original again afterwards,
and constructs graph records from the pinned worker's drafts. The existing
transport has a clean environment, closed inherited descriptors, output and
wall bounds, and child cleanup. The worker applies CPU/address-space limits.
It does not execute source code, formulas, macros or scripts. This is not a
sandbox against a malicious administrator changing the installation itself.

XLSX: workbook → named sheet → derived populated-row range → cell. A range is
not a claim that an Excel table was declared. Formula cells retain an explicit
unsupported structural status; cached formula values are never observations.
ODT: raw ZIP, derived UTF-8 and extraction-JSON Sources; nested overlapping
recognized blocks retain member/XML locators and repeat information.
EPUB: ordered spine chapters and recognized blocks, including empty/repeated
blocks. Basic XHTML only; no rendered CSS layout, pagination, image or math
interpretation. Structural extraction is partial and derived.

The original ZIP bytes and exact derived text bytes are separate blobs.
Structural Units point into the extraction JSON; quoted occurrence Units point
into derived UTF-8, never ZIP offsets. Native IDs bind source key, version,
method and occurrence. Selection/read replays the installed parser and validates
persisted source, method, receipt and quote. Changes to a pinned helper/profile/
interpreter refuse old replay; this increment does not implement a parser
migration registry. Cached projection reuse is not a full-subgraph repair audit.

ZIP uses the same bounded CRC/path/alias validator as the EPUB reader, without
extracting paths to a filesystem. Native archive/folder/file Units retain parent
links and member Sources. Admitted members recursively use `source_parts`; an
unadmitted member remains an original with an explicit partial status. Installed
limits bound depth, cumulative member bytes/count and individual parsing.
Native SQL savepoints cover recursive writes. Failed imports can leave
unreferenced content-addressed blob bytes: physical blob rollback is not claimed.

The existing SourcePartsPanel exposes upload, explicit materialization,
recognized structure, archive-member navigation, original/derived downloads,
GraphView and the existing **account-global** selector. It is not a per-request
or per-conversation selector. Source listing is a bounded window.

Acceptance uses `loom/web/e2e/file-graph-native.mjs` with real installed native
server/helpers and a loopback synthetic provider, plus the focused existing-C-API
risk checks in `loom/tests/file_graph/verify_native.py`. Author parser suites are
historical evidence, not native acceptance. See the dated integration report for
actual results. OCR and real vision are separate; a synthetic provider never
constitutes vision acceptance.
