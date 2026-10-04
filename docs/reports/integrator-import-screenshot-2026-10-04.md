# Integrator9 — negative screenshot replay, 2026-10-04

Source: `03cd52e33e5902ffb90d33ec0084cea2bf60feaa` (W5).
This result is withheld from main. The separately measured checkpoint fix passed
2 scenarios /29 independent checks; it does not close this screenshot regression.

The actual production importer and OCR provider were run on a synthetic 1×1 PNG
with ScriptedTransport. The no-provenance control sends `data:image/png;base64,…`;
provenance `import_file` and direct `import_screenshot` both send
`data:image/;base64,…`. MIME preservation is **1/3**, with two negative routes.
The **11/11 diagnostic checks** establish the reproduction; they are not passing
acceptance tests. The mock OCR response intentionally succeeds, so its success
does not show that a live provider accepts the malformed URI.

The source snapshot has no filename extension: importer_core.cpp:354–364 and
493–514 select the immutable blob; provenance.cpp:66–70 names it by hash;
importer_text.cpp:336–342 passes that path to OCR. media_providers.cpp:280–283
derives the format solely from the extension;211–213 constructs the malformed
URI. No original-filename, MIME or signature fallback is present.

All211 actual source/header files match the pinned public Git blobs before/after;
all3 actual linked archives remain unchanged. Core SHA256:
`f2002cbe0a527039bf9b1c03753c4707f5a85d0667da70b2cee805233f83a9c0`.
One probe compilation:6.408s /343040KiB; execution:0.025s /12880KiB.
Zero production translation units compiled; zero live/paid calls or private data.

[Full evidence and portable replay](integrator-import-screenshot-2026-10-04/evidence.zip)
contains original causal source, tiny PNG, all request bodies, commands/results,
source/library/binary hashes and the exact executed runner. ZIP SHA256:
`c840cc9ca0948253460478918ced73bc13155cab65d281c686964e81129fcfae`
(79707 bytes). The portable runner was syntax/help checked; measured execution
belongs to the retained host-path runner.

## Do wątku N

- **5:** preserve original declared image format while reading immutable bytes;
  retain source/byte provenance. Add strict offline MIME regressions for both
  import_file and direct import_screenshot, including other supported formats.
  Reopening the mutable original merely to recover its extension loses the new
  provenance guarantee. Return the corrected source for this exact replay and
  complete mixed-W2 gates.
- **11:** if the media API needs an explicit format input, coordinate that small
  interface with5; this handoff does not give5 ownership of media/provider code.
- **9:** keep the checkpoint-positive and screenshot-negative evidence separate;
  hold W5 until this introduced regression is fixed.
