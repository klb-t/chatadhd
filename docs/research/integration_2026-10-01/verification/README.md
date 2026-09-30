# W1–W6 integration verification

Evaluated native source: `da77c769d3e020c396db5bef6a3a3755314d75c0`,
tree `abe386316d6fc66a3fa2b0ba8aabc2b28c8df41d`.
Later commits add documentation and browser evidence/harness guards; they do not
change the evaluated native or production web implementation.

| Gate | Fresh result | Evidence |
|---|---|---|
| Complete native build | PASS; Debug, warnings as errors, bundled SQLite 3.47.2 | `build-corrected.log`, corresponding receipt |
| Full CTest | **94/94 entries**, 0 failed/skipped entries, **111.01 s** | `ctest-first.xml`, `ctest-first.log`, `ctest-summary.json` |
| Research structure | **841 tests** passed, included in CTest | `ctest-full-output.log` |
| W6 exact import V2 | **24/24 named checks** | `w6-import-v2-first/receipt.json` |
| W6 transport | **72/72 checks**, six captured loopback requests | `w6-transport-first/` |
| Browser chat/context | Agent-run PASS, five local fake-provider requests, zero remote | `web-native-manifest.json`, `web-chat-context-headless.log` |
| Browser workspace | Agent-run **10/10 scenarios**, zero model requests | `web-workspace-headless.log` |

These denominators overlap and must not be added. CTest entries are suites, not
individual assertions. Its per-source doctest filtering reports other source
cases as unselected; the zero-skipped claim above concerns CTest entries.

The four new native suites are included in that one full run: durability
8 cases/106 assertions; independent durability audit 6/76; chat retrieval plan
8/259; import fidelity 5/327. Actual W2 serialized request bodies plus returned
and stored traces are in the five `w2-evidence-first/*.json` files. These use the
native fake transport and authored synthetic inputs; browser and W6 evidence
separately exercise an actual loopback HTTP boundary.

The existing catalog synthetic DEV gate also passed unchanged: 31/45 relevant
conversations selected, precision 31/31, traps 0/5, generic noise 0/15, ranking
AUC 0.965556 and hits@45 42/45. Complete metrics and W2 DEV rows remain in
`ctest-full-output.log`; JUnit truncates some individual stdout fields.

## First failures and corrected results

The first build evaluated `8cc5a5648ada511053d57a0f3979d96c5e0f1d36` and failed
after 191.49 seconds because the new import test had an unclosed nested JSON
initializer. `build-first.log` and its source/binary receipt are retained.
The nested-ZIP locator correction and test syntax correction were integrated
at an explicit build safe point. The final incremental build passed in
51.82 seconds with source hashes unchanged. There was no baseline rebuild.
The previous binaries were preserved separately; their hashes match the prior
continuation receipt and are listed in `preserved-previous-binaries.json`.

The first full-Chromium browser attempts failed before page launch because
the process-singleton socket was disallowed (`EPERM`), with zero provider
calls and no product assertions executed. Original logs and partial records
remain in `web-*-first`. The installed Chromium 1194 headless shell then passed
both suites against the same native binaries. The browser manifest separately
records executed script hashes and the later directory-collision guard change;
there was no additional product-matrix run after success. Browser evidence was
run by the web agent and its saved artifacts/hashes were checked by this lane.

## Frozen W6 comparison and remaining negatives

The W6 instruments and inputs were not changed. The prior saved `b118c80`
measurement was V2 23/24; the fresh `da77c76` result is 24/24. That is a bounded
synthetic structural comparison, not whole-export or model-quality evidence.
V2 includes 12 oracle controls, nine raw-message comparisons and three complete
conversation reconstructions. Native source-array order is now preserved.

The V1 diagnostic run invoked inside V2 reports **54/59**, versus its saved
historical 47/59. Its five remaining negatives are preserved verbatim:

- Two `member_bytes:conversations.json` checks require separate member blobs;
  preserving the complete ZIP still permits recovery of those member bytes.
- Three wrapped OpenAI locator checks resolve document-root pointers against
  an already unwrapped array. The new native wrapper-root regression is part
  of the five-case import fidelity suite; the older oracle was not rewritten.

The frozen import receipt's `baseline_requested` remains
`b118c80e981c08ec6d7f9ab6aacc177979186cf2`, inherited from its immutable manifest.
It is **not the source of this execution**. The adjacent stage receipts and
`results.json` bind the actual execution to `da77c76` and CLI SHA-256
`007c429d8b637a0b1dca3ef263a18c00e254c4a38b92e66e80afee8bfb6a4937`.

W6 transport used fixed synthetic replies. Its six stored traces survive reopen
and reindex; one trace represents a compiled but unsent missing-key request.
A compilation trace alone does not establish dispatch. No private archives or
model calls were used in this integration verification.

## Reproduction and artifact integrity

Every `*.receipt.json` records the exact command, environment, evaluated source
pin, checkout HEAD, source hashes and binary hashes before/after that stage.
Configure used the local CMake 4.4.3/Ninja installation, Debug `dev` preset with
`LOOM_USE_SYSTEM_SQLITE=OFF`, server enabled, build parallelism 6 and CTest 4.
Python used the existing local dependencies and user base; `TMPDIR=/var/tmp`.

`capture_stage.py` is the recorder as executed from
`/workspace/scratch/a371a1ca13b1/verification/run_night_stage.py`; its paths are
relative to that scratch layout. It refuses to overwrite an existing stage.
Reproduction must use a checkout matching the recorded source and fresh output
directories. The exact frozen W6 commands are in the adjacent stage receipts
and the original W6 reports; restore ZIP inputs using the existing
`import_decode_fixtures.py`, never the fixture-generation or freeze actions.

`manifest.json` hashes this text-only evidence package. It excludes itself.
The two generated ZIP inputs remain outside the package; their committed
base64 representations and immutable manifest preserve the originals.
No executable, database, runtime directory or private source bytes are included.
Browser source, commands, payloads, first failures and asset hashes are detailed
in `web-native-manifest.json` and [the web receipt](../WEB_PLAN_RECEIPT.md).
