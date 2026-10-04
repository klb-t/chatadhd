# W5 — owner nudge 21:06, 2026-10-04

Current code `4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2`, freshly rebased
onto main `30ad7d37337d6641cb7714b03e9feff0e6e25d25` (accepted W2 + R39–R41).
Full fresh GCC/WERROR/vendored/shared/CLI/server/test build completed.
First full CTest was113/115: two server process launches failed on an invalid
generated binary. Relinking only that output from unchanged objects/libraries
fixed the focused server2/2. A new, separate full115-entry run passed.
**This increment is ready for integration review.**
No live or paid provider calls; synthetic/public input only.

| Evidence | Actual result | Scope |
|---|---|---|
| [Screenshot focused](screenshot-focused/README.md) | before2/6, after6/6 cases /872 assertions | changed objects + historical archive; strict offline transport |
| [Migration race](migration-race/README.md) | before6 failures, after11 cases /189 assertions | changed object + historical archive; deterministic other writer |
| [Regular audit](audit/README.md) | 19/19 cases | ordinary suite + existing CTest discovery adapter, not full CTest |
| [Independent OCR](screenshot-replay/README.md) | MIME3/3,22/22 checks | actual fresh W2+W5 core; original probe unchanged |
| [Independent checkpoint](checkpoint-replay/README.md) | 2/2 scenarios,31/31 checks | same actual core; no implementation overlay |
| [Web](web/README.md) | offline install/build exit0,85 modules | 79 unchanged tracked inputs,4 hashed assets; no browser tests |
| [First mixed failure](mixed-native-first-failure/README.md) | 113/115, followed by focused server2/2 after relink | original failure retained, no source/test changes |
| [Full mixed native](mixed-native/README.md) | 115/115; native706/26,375 assertions, Python1295/0skips | exact source4e8c3de/main30ad7d3,1203 files and9 binaries stable |

Each directory preserves commands, original stdout/stderr, source/library
bindings and lossless hash manifests. Failed diagnostics stay separate from
passing acceptance checks. The independent OCR/checkpoint replays pass on the fresh actual core.
The final full CTest rerun passed independently of the first failure.
Six new native groups ran47 cases/1910 assertions; the audit wrapper ran19 Python
cases. Existing optional catalog_scale ran0 cases, explicitly disclosed.
The original failed receipt is immutable. All52 data/profile migrations are
not completed by this increment.
