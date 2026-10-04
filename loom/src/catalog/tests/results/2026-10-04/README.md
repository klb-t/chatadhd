# Catalog evidence, 2026-10-04

These are preserved measurements, including failures. They do not mark the
branch ready for integration. See the [thread report](../../../../../../docs/reports/catalog-selection-2026-10-04.md)
for results, limitations and handoffs.

- `dev-before.json` and its inputs record the frozen public DEV baseline.
  `dev-after.json` records the final native build with the new channel disabled:
  all 68 decisions/features/reasons match. Recall remains 31/45.
- `mechanism.json` records label-derived mock vectors, not model quality.
  The 14/14 admissions verify the seam; noise without supplied vectors does not
  measure semantic rejection. No providers were called.
- `native-tests.log` records 15 cases and 228 assertions passing; the empty
  compile log reflects successful strict compilation. Source and binary hashes
  are retained in input receipts and `native-binaries.sha256`.
- `ctest-full.log` records the final gate: 104/106, two research timeouts.
  The structure/contracts direct logs and tmpfs log are diagnostic runs only.
  The tmpfs run is excluded from final verification under the handoff's rule.
- `blind-first-look/` preserves the one final evaluation and all decisions
  written before gold. Its own manifest binds the original evidence files.
  The lexical-trap quality condition fails; the profile-transfer limitation
  applies. Do not run its harness again for this task.

Raw fictional blind exports and labels stay at the pinned public corpus commit;
runtime databases and binaries are not required in this repository. Reproduce
the DEV/native checks with the commands in the thread report. Reading preserved
measurements does not constitute a new blind evaluation.

The rejected nearest-seed simulation, complete inputs and replay live on
`archive/2026-10-04/catalog-nearest-seed-negative`. They were not introduced into
production source or the default policy.
