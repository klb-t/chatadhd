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

The **historical 2026-10-04** full CTest passed **122/122**. The unchanged thread8 guard verifies **776 native cases,26,758 assertions and1,276 Python cases**, with no Python skips and the existing explicit unexecuted `unit.test_catalog_scale`0/0 entry; see [coverage-full.json](evidence/final-validation/coverage-full.json). The original [positive JUnit](evidence/final-validation/ctest.xml) and [negative truncation coverage](evidence/final-validation/coverage.json) remain preserved. The [derived full-output XML](evidence/final-validation/ctest-full-output.xml) restores16 truncated outputs from the same actual raw log; [byte-offset/hash provenance](evidence/final-validation/output-restoration-receipt.json) proves122 matching records,106 exact original streams,16 exact1024-byte prefixes and unchanged metadata, without rerunning tests. Integration evidence is recorded in the separate [final receipt](evidence/final-validation/receipt.json).


The [2026-10-05 continuation](../data-profiles-2026-10-05.md) is rebased onto main `e4109df` and has a new full **127/127 CTest PASS**, native/web builds PASS. Both unchanged thread8 evidence guards agree on **783 native cases, 26,985 assertions, 1,371 Python cases, zero Python skips**, with the sole existing catalog-scale opt-in explicitly unexecuted. The complete original JUnit required no output restoration. The [current receipt](evidence/continuation-2026-10-05/final-validation/receipt.json) verifies all 735 source/data/test hashes and binds the actual raw inputs. This branch has not been accepted on main by thread9.

The full-core CLI comparison preserves all 19 shared profile values/schemas/hashes and exact help/version bytes, adds three import domains, and creates no application files. Import domains remain **UNWIRED**. Canonical usage defaults now derive from the single thread2 policy source; active config wiring remains separate. Historical 695/213/187/19 totals and per-ID locators remain frozen; the JSON appends a separate continuation.

The [new R42 literal mechanism](implementation-product-literal-guard-2026-10-05.md) has 41 positive synthetic tests but its full repository audit is **negative**: 56,968 unclassified lexical literals and 85 blocked files. The full compressed JSON, all source hashes, owner debt and negative replay evidence remain retained. Lexical candidates are distinct from the original reviewed groups and are not automatically forbidden product data.

## Do wątku N

- **9:** retain both historical and current receipts; main acceptance requires your actual integration gates. Assign the literal-audit owner debt without weakening the guard.
- **1–6/10/12:** use the checked profile definitions/defaults/provenance and preserve remaining caller wiring, explicit exclusions and exact source identity. Data readiness does not activate a consumer.
- **8:** the literal guard is ready as a mechanism; actual repository acceptance remains negative until individual classifications and blocked syntax coverage are resolved.
