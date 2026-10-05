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
