# Optional web test capabilities — 2026-10-04

This is offline CI-helper verification, not execution of the pending W10 UI.
The source commit and hashes of the uncommitted followup helper, tests and
workflow are pinned in [receipt.json](receipt.json).

- [tests.log](tests.log): **6/6 process fixtures**. They verify exact argv and
  working directory, absent capability without execution, nonzero child exit
  propagation, stop after failure, unavailable executable and malformed package.
- [process-probes.json](process-probes.json) and
  [process-probes.log](process-probes.log): real npm executes one temporary local
  Node fixture whose script name contains a leading dash, spaces and shell
  punctuation. No external packages are installed and no provider is contacted.
- The actual checked-out `loom/web/package.json` declares neither
  `test:interface-2` nor `test:interface-2-native`: **0 W10 product scripts
  executed**, with two explicit `CAPABILITY_UNAVAILABLE` diagnostics.

The workflow keeps its existing checks and appends the caller-configured script
sequence after Playwright installation and the existing web tests. Each
declared script is started through `npm run -- <name>` in its package directory;
failure makes the step fail. Missing scripts are reported as unavailable on that
source version and never represented as executed tests. No remote CI was started.

Workflow-only followups also trigger push/PR CI on
`.github/ctest-evidence-policy*.json` changes and explicitly pass the CMake cache
path plus the caller-selected compiler/build/SQLite/shared/WERROR fields to the
build receipt. That receipt's parser is independently owned and verified by the
parent task; this helper proof does not claim its execution.
