# PDF structure projection

This internal adapter consumes `chat::read_document_text` output. It neither
decodes PDFs nor repeats Poppler/PDF-to-pages functionality. `projection.py` is
an executable Python stdlib helper; `adapter.cpp` connects it to existing
SourceRecord, model::Unit/Observation, provenance and source_parts/genesis.
The separate integration patch supplies the shared hook and data presets.

The original PDF remains an immutable blob. The derived UTF-8 blob preserves
the exact Poppler output, including page form feeds, whitespace and line endings.
Every nonempty byte of that derived representation belongs to exactly one block
cut. A block's quote range excludes separators; its genesis range retains them.
Blank pages have page Units and, where bytes exist, page_gap blocks. Repeated
text has distinct page/block locators even if genesis fingerprints match.

Page Units use the original source with a logical `member=page:N` ordinal and
no PDF byte offsets. `member_semantics=pdf_page_ordinal` explicitly distinguishes
that locator from an archive member. Their attrs point to the derived source and
its exact byte range, and to `/pages/N` in the saved extraction JSON blob.
Their default `bytes=0` means unknown PDF-page byte size, with
`source_bytes_known=false`; it is not a measured zero-byte page.

Paragraph/heading quote observations address only the derived UTF-8 source.
Original page/block and page-relative **derived text** byte ranges stay in attrs.
Paragraph cuts and chapter candidates are inferred structure. Heading candidates
produce Field quote observations, not observed Heading facts. Chapter Units and
their membership links carry inferred evidence and method identity. Candidates
may span pages; page breaks do not assert paragraph continuation or its absence.
There is no claim of layout, tagging, reading-order correctness, OCR or complete
document understanding, and no calculated confidence score.

`profile.json` is installation-owned policy; `preset.json` retains the author's
projection defaults as data. The profile pins helper/worker/profile/interpreter
hashes in the runtime manifest. Request JSON cannot supply a receipt, executable,
profile or pack. Resource limits must be positive: a zero value cannot disable
the bounded host transport. The default is an 8 MiB original/JSON/output bound,
3 s projection CPU, 256 MiB address space and 5 s subprocess watchdog. These are
process-safety presets, not the owner's format-onboarding performance guidance.
Poppler extraction itself remains the existing in-process reader; this change
does not claim a new CPU/time sandbox around Poppler.

The receipt binds original SHA/size, exact derived text, page/block ranges,
options and the installed helper/profile/interpreter manifest. It does not
claim a measured Poppler version or whole-system library attestation. Read and
selected context re-extract the ORIGINAL PDF and compare the fresh derivation;
replaying an internally consistent client-supplied text receipt is insufficient.
Historic script blobs are never executed. Different pinned installations refuse
old method replay; retain the matching package until an explicit migration.
Cache is not an audit or repair of every graph edge. SQL rollback does not
promise physical removal of every content-addressed blob.

The current reader's joined text cannot distinguish an in-page form feed from a
page separator. A mismatch between form-feed count and actual reader page count
therefore fails as ambiguous. A text-layer-empty PDF remains unavailable; it is
never treated as OCR output.

The native adapter uses the EXISTING DOCX `read_local_json_bytes` transport,
with a host-created anonymous input fd. No second subprocess/watchdog stack,
shell, document-chosen path or source-checkout fallback exists. The package
resolver is shared with DOCX/CSV/AST; the PDF package has its own manifest so
adding PDF does not change the accepted CSV/AST installation binding.
Linux/glibc >=2.34, Python >=3.11/resource and the existing Poppler-GLib dependency
are required. Output, wall time and cleanup share the current transport, while
the installed worker sets CPU/address-space/core limits before parsing.

Run the executable helper tests with:

```sh
python3 -B -m unittest discover -s loom/tests/pdf_structure -p 'test_*.py' -v
```

`reader-output.json` is a synthetic existing-reader result bound to
`original.synthetic`; that source stub is intentionally not a decodable PDF.
`projection-fixture.json` is its deterministic expected projection. Native tests
also use the existing synthetic `documents/mixed-text-blank.pdf` through the real
Poppler path. Native/backend/HTTP execution belongs to the sole integrator's runner,
after merging the shared hook and regenerating the changed text pack.
The integrator replaced author helper-path configuration with the default
package resolver; native tests now use that installed helper, not the checkout.
The runtime profile embedding is unchanged because PDF projection options live
in the separately pinned installation profile. No browser/UI acceptance is implied.
