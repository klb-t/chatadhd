# Synthetic filesystem consistency investigation, integrator9, 2026-10-05

Source0a81480 (same production code66da570). First workspace-backed BEFORE1 failed with SQLITE_CORRUPT; a fresh workspace replay returned a scorecard but left a corrupt main DB. Both actual vendorSQLite3.47.2 and PythonSQLite3.53.1 reproduce the corruption. All raw DB main files without WAL are intact; valid WAL-prefix materializations reproduce the issue.

The exact same tool/CLI/configuration using a /tmp working directory passed: all6main/FTS databases, quick_check+integrity_check via both independent readers =24/24. 305sources and actualcore/CLI hashes stable; scored metrics identical after excluding only work_dir and generated conversation IDs. This supports a workspace DB/WAL consistency hypothesis; the exact restoration/write mechanism remains unproved. No production-source change, threshold weakening or paid/live call.

[Complete85-file ZIP](synthetic-consistency-evidence.zip), SHA25652a0f3c14856c7d4609c1b7629926fc3a7e1dee88abdb0ae7f3bde0b41e732a2,5847948B. Includes original failures, fresh workspace replay, all raw synthetic-only DB/WAL/SHM/config sets, healthy/tmp control, exact helpers/probe source and commands, source/binary bindings and explicit inference. Binaries/libraries reproducible from hashes/sources are not republished. Negative-bearing evidence stays in this archive, notmain. BEFORE/AFTER1 quality must use identical explicit physical-working-directory policy with integrity validation.

## Do wątku9

Keep full raw failures. Do not equate scorecard exit0 with database integrity or call this an application-code fix.
