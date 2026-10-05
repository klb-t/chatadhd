# Final documentation-only main rebase — fresh CTest receipt

Base `ba6eaf6244d2d4136fbb3534bc0d45a4900eeda1`, after the previous
`7282437b1c88933977f64b3468b9f42f7b400494` checkpoint. The sole base change
is OWNER_REQUIREMENTS R39–R41. This is an additional receipt; earlier
verification, original capped XML and negative guard outcomes remain intact.

A successful WERROR rebuild and fresh full CTest **108/108 in 292.08 s** are
retained here. The unchanged W8 guard establishes **107 executed entries,
659 native cases, 24,465 assertions and 1,276 Python cases**, with zero Python skips
or errors. `manifest.json`
records observed timings and real inner counts, rather than inferring case
coverage from outer passes. `identity.json` independently compares all recorded
sources, six runtime/test binaries and both static link libraries with the
previous receipt. They are byte-identical. The previous 25 focused groups,
5 generator cases and 4 source-edit variants remain applicable to those exact
inputs; this receipt does not claim they were rerun.

The original JUnit has capped output and its unchanged W8 coverage guard is
retained as a negative result. `restore_ctest_output.py` reconstructs only capped
stdout from the matching same-run LastTest log, preserving all result metadata.
The same pinned guard verifies the reconstructed complete XML. The existing
opt-in `unit.test_catalog_scale` is explicitly unexecuted.

Raw build/CTest text, original/full XML, LastTest and CTest list use the existing
`loom.raw_log/1` gzip/base64 format. Decode as documented in [the previous
receipt](../README.md), verify its original-byte hash, then run the committed
restoration helper and the pinned W8 guard using `verification-commands.json`.
No tests, timeouts, statuses or product thresholds were changed. This build
does not include other lanes' unmerged code. All fixtures remain offline.
