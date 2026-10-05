# Data-in-code inventory — 2026-10-04

Pinned baseline: `161cc22dfb84fe863389d6b90323bd44516a68dc`. The inventory was completed before executable source changes. Every required native/server/CLI/web/Android surface was scanned, with file hashes and literal line coverage. Additional retained Python and tooling surfaces were scanned too. The per-thread tables contain reviewed actionable groups, not a claim that every matched literal is a product policy. Generated embeddings and test fixtures are classified as such.

695 reviewed groups, 559 tracked code/presentation files, 39496 mechanical candidate rows.

- [Thread 1](thread-1.md)
- [Thread 2](thread-2.md)
- [Thread 3](thread-3.md)
- [Thread 4](thread-4.md)
- [Thread 5](thread-5.md)
- [Thread 6](thread-6.md)
- [Thread 7](thread-7.md)
- [Thread 8](thread-8.md)
- [Thread 9](thread-9.md)
- [Thread 10](thread-10.md)
- [Thread 11](thread-11.md)

Android, retained Python and legacy C ABI mirrors have ownership gaps; thread 9 must assign them before edits. No private archives, secrets, paid requests or sealed answer key were read.

The followup source audit covers all **213 original thread11 groups** in [migration-status.json](migration-status.json): **187 migrated, 19 partial, 5 retained wire, 1 retained algorithm, 1 deferred to another owner**. Each ID records the current destination, exact source references, retained contracts and remaining caller work. The model creation API/data is available, but external producers remain unconnected; the 19 partial entries are not counted as complete. Three canonical bootstrap domains are separately data-ready with core wiring pending. Post-baseline thread8 additions are recorded in [branch-supplement-thread-8.md](branch-supplement-thread-8.md) and [branch-supplement-thread-8.json](branch-supplement-thread-8.json), outside the original 695/213 totals. Full current build/CTest evidence is recorded separately by the integrator.

The actual full CTest passed **122/122**. The unchanged thread8 guard verifies **776 native cases,26,758 assertions and1,276 Python cases**, with no Python skips and the existing explicit unexecuted `unit.test_catalog_scale`0/0 entry; see [coverage-full.json](evidence/final-validation/coverage-full.json). The original [positive JUnit](evidence/final-validation/ctest.xml) and [negative truncation coverage](evidence/final-validation/coverage.json) remain preserved. The [derived full-output XML](evidence/final-validation/ctest-full-output.xml) restores16 truncated outputs from the same actual raw log; [byte-offset/hash provenance](evidence/final-validation/output-restoration-receipt.json) proves122 matching records,106 exact original streams,16 exact1024-byte prefixes and unchanged metadata, without rerunning tests. Integration evidence is recorded in the separate [final receipt](evidence/final-validation/receipt.json).
