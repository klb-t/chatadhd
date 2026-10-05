# Final rebase closing gate — 2026-10-05

Tested the current research code after rebase on main `e4109df7e4af22b461def5f7d62e268d9b9a8825`. The local tested commit `cd378a1e72a57776f912547ad4e95ac115b0862e` and published API commit `459846915d01fe084c80718812cda7dfd37ebfe6` have the exact same tree. The metadata transition during this run changed no tested files.

Existing GCC Debug `-O0 -g0`, Werror, vendored SQLite and OpenSSL shared build was reconfigured and incrementally rebuilt: configure PASS, 51-step build PASS (115.53 s), all 240 object files had ELF magic and all six recorded binaries were executable. This is not a clean-build, full compiler matrix or web-build claim.

Full CTest was run once against its dynamic 110-entry registration, with no test exclusions, threshold changes or retries. **110/110 PASS, exit 0, 303.54 s.** All 110 complete stdout results match JUnit and LastTest summaries. Counts: 82 native suites, 663 executed native cases, 24,489 passed assertions; 26 Python unittest suites, 1,715 executed cases; both CLI and server smoke suites completed. Total 2,378 executed unit cases plus two smoke suites. JUnit has no failure, error or skip elements; no Python skipped events appear. Complete logs are retained without truncation.

**The stricter every-suite-must-execute-cases audit is blocked.** `unit.test_catalog_scale` reports zero executed cases/assertions, with 664 filtered-or-skipped cases. Its unchanged main source explicitly skips the ~1 GB slow test unless `LOOM_RUN_SLOW_TESTS` is set (`loom/tests/test_catalog_scale.cpp:58,63–64`). All other native and Python suites executed positive case counts. The source file, CMake registration and exact command are retained in `existing-main-zero-case-finding.json` and the source copies. The aggregate 664 includes file filtering; it is not a count of 664 executed skips. Do not describe this run as having no native skips or as a fully green stricter audit.

All 1,097 inventoried functional files and six explicit public research policies stayed byte-identical across configure/build/test. The six binaries stayed byte-identical during CTest. Protected `real-holdout-key` paths were excluded without reading. No key, billing ledger or model responses were accessed. No paid operation, source fix or extra rerun was performed.

The native zero-case finding is handed to threads 6/8/9. Thread 7 does not edit their code or relax the audit. `receipt.json` intentionally retains `gate_green: false`, while recording the complete successful CTest result. Resume at that cross-scope audit finding before claiming the integration gate fully green.

`run_final_gate.py` is the exact offline verification capture script. Its snapshots and per-phase receipts preserve source hashes, command arguments and complete stdout hashes; `artifact-manifest.json` seals the evidence files and `WHITELIST.json` identifies all files to retain, including Git-ignored raw logs/cache snapshots.

`LastTestsFailed.log` is a retained **stale prior failure index** (`99:research.structure`), modified before this gate; CTest did not remove it after the successful run. Its dated provenance is in `stale-LastTestsFailed-provenance.json`. Current stdout/JUnit/LastTest all record 110 passed entries.
