# Full native and Python gate, 2026-10-05

The unchanged complete CTest gate passed 108/108 in 128.72 seconds, with zero skipped entries. It ran 81 native suites (659 cases, 24,465 assertions), 25 unittest suites (1,582 cases, zero skip events), and two additional CLI/server smoke suites with required completion markers. That is 2,241 explicitly counted unit cases plus two custom smoke suites; their assertion counts are kept separate.

Tested HEAD was `467b153a4729d1742f6d9c77470e0e028d0a0f5d` with an uncommitted functional candidate. `published-code-binding.json` verifies every tested functional source and both public billing policy bytes against published code commit `165e307e0cc390dc63cb6139d353d2f6ef603dcf` (helper ancestor `dd795af346e0e1f3b88189093fa99438582a49ba`). The complete before/after maps show zero functional changes during the run. All six observed binary hashes/modes stayed unchanged, and native Git inputs match configured build `70587e4d12fac9fa0042e554dbeb7a4aa60127ba`.

`receipt.json` points to raw CTest, JUnit, LastTest, exact counts, source maps, and build provenance. `audit_native.py` reproduces the recorded workspace hash/count checks. Caller PYTHONPATH and TMPDIR were unset; registered CMake test environments were retained. Native configuration was Debug `-O0 -g0`, WERROR on, vendored SQLite, shared/CLI/server/tests on.

Earlier complete 107/108 negatives and initial generated-object/tool-mode build boundaries are preserved in the separate archive-only native boundary ZIP. The recorded continuation built successfully after generated-object repair; this evidence does not claim a later independent clean rebuild. The existing optional catalog-scale stress case remains source-disabled by default; all 81 registered native suites executed.
