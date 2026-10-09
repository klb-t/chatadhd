# Watchdog independent acceptance suite

The suite imports the requested checkout's actual TypeScript consumers. It does not copy the implementations into audit code. SQLite is file-backed and is closed/reopened in durability tests. All values are synthetic software fixtures. No provider request is sent.

Requirements: Python 3, Node with the checkout's pinned `npm ci` dependencies, including a working `better-sqlite3` binding. Run dependency installation in an isolated checkout/build directory, not a product worktree with unrelated changes.

```bash
python3 tools/ecosystem-audit-2026-10-09/pass2/watchdog/run.py \
  --repo /absolute/isolated/Watchdog-JH16 \
  --sha 58a0c93bd0135e3715dcbc4d92fb80e61bd31215 \
  --out /absolute/scratch/watchdog-receipt.json \
  --scratch /absolute/scratch/watchdog-runtime \
  --mode both
```

Baseline: **21 tests; reproduction 6 PASS; acceptance 9 PASS / 6 FAIL; exit 1.** PASS reproduction means a defect is present. FAIL acceptance is a real failing assertion, never xfail. `--mode reproduce` and `--mode accept` separate the gates. `--filter WD-003` runs both tests for that finding; `--filter WD-FLOW` runs five durability/permission/export tests. A filter matching no test exits 2. `test-index.json` beside the report enumerates every test.

The runner requires HEAD to equal `--sha` and refuses tracked differences. To test a fix, use another isolated checkout and its SHA; do not rewrite the baseline checkout. The same assertions are used against baseline and fixes. Unknown-template acceptance is deliberately narrow: rejecting unknown identity does not prove the whole template or prompt extraction is complete. The parameter acceptance permits a meaningful explicit unsupported/conflict error rather than silent omission.

The independent suite blocks global fetch, HTTP(S), datagram creation and socket connect; expected transport is injected in memory. The separate `loopback-guard.cjs` permits only loopback for the project's existing HTTP tests. It is not the independent suite's weaker replacement.

Existing host gate, run in the isolated checkout after `npm run build`:

```bash
npm run lint
NODE_OPTIONS="--require /ABS/tools/ecosystem-audit-2026-10-09/pass2/watchdog/loopback-guard.cjs" \
node --import tsx --test \
  tests/integration/settings.test.ts \
  tests/integration/source_access.test.ts \
  tests/integration/research_package.test.ts \
  tests/integration/workbench.test.ts \
  tests/integration/workbench_access_revocation.test.ts \
  tests/integration/unified_search_revocation.test.ts \
  tests/integration/accounts_upgrade.test.ts \
  tests/unit/output.test.ts tests/unit/llm.test.ts
```

Observed: typecheck, build and **66/66 existing host tests PASS**, no skip. Browser executable is now available but actual launch aborts at Chromium `process_singleton_posix.cc:292`: `socket() failed: Operation not permitted`. No browser assertions ran. This is distinct from a missing compiler/browser and from host acceptance.

The report preserves `receipt-core.json`, `receipt-flow-attempt1.json`, `receipt-flow.json`, and final `receipt-complete.json`. The first flow attempt used an incorrect synthetic resource path; the product correctly refused it. The corrected fixture derives the path from the real profile; no product change was made. The final receipt records the exact suite SHA-256. Large temporary DBs, objects, browser install and build output remain disposable scratch data.
