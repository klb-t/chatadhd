# Extended application profiles — measured verification, 2026-10-04

Hosted implementation commit: `95e5049382a6025ca0c591e9b6dba49f90af3946`.
Implementation tree: `258f380ba916616aa5b95edb0065ecec5b9703ac`.
Base: `9d15d2dd0733274356f768816e207e26e13845e4`, the earlier profile increment
integrated through [PR8](https://github.com/klb-t/chatadhd/pull/8).
The extension and this evidence are delivered through
[PR9](https://github.com/klb-t/chatadhd/pull/9). Subsequent documentation commits
do not alter the measured implementation. [Source, bundle and binary hashes](source-sha256.json)
pin the tested inputs; [native validation summary](native-validation-summary.json)
pins configuration, test names, stages and native binaries.

## Results

| Check | Measured result | Evidence |
|---|---|---|
| Strict TypeScript and production Vite build | Pass; 85 modules | [build](build.txt) |
| Profile runtime and API adapters | 19/19 and 9/9 groups | [offline profiles](test-application-profiles.txt) |
| Python contract suite | 209/209 tests, including 12 profile tests | [contracts](python-contracts.txt) |
| Imported source projection | 8/8 groups | [state](test-imported-message-content-state.txt) |
| Imported source component in Chromium | 1/1 group | [component](test-imported-message-content-browser.txt) |
| Actual native import → ChatView | 4/4 groups; zero remote media or inference requests | [native source](imported-source-native.txt) |
| Profile serializer → actual Python codec → HTTP/native store | 11/11 groups; two native receipts; restart, replay and exact source restoration; configuration unchanged; zero model calls | [native graph](native-graph-results.json) |
| Native server and real profile UI in Chromium | 15/15 groups; six local fake-provider calls, zero remote calls; configuration unchanged | [profile browser](native-browser-results.json) |
| Existing web regression suite | 16/16 steps | [existing browser](existing-browser-suite.txt) |
| Transport, workspace state and retrieval-plan state | Pass; workspace 8/8 | [transport](test-transport.txt), [workspace](test-workspace-state.txt), [context plan](test-context-plan.txt) |
| Native GraphPacket FFI regressions | 18/18 tests | [FFI](graph-packet-ffi.txt) |
| Native server smoke/auth | Four sections pass, including unauthorized POST followed by authenticated POST on one persistent client session | [smoke/auth](server-smoke-auth-r4.txt) |
| CTest coverage | All 108 discovered entries passed across **106 + 2 stages**; zero omitted entries | [106 entries](ctest.txt), [CLI/eval 2 entries](ctest-cli-eval.txt), [108-entry manifest](ctest-manifest-108.json) |

CTest was initially configured with CLI disabled and passed 106 entries in
273.74 seconds. Enabling and building the CLI exposed `cli.smoke` and
`eval.harness`; both passed in 3.74 seconds. The names from both stages exactly
cover the final 108-entry manifest. This is not a single 108-entry invocation,
and historical results are not substituted for fresh coverage.
The existing 16-step browser regression preceded the final auth/header and
capability-label edits; the final profile, graph and imported-source suites ran
against the final bundle/server. No GitHub Actions or paid model calls were used.

## What the checks establish

Profiles are versioned data with explicit target identities and adapter guards.
Declarative inputs/context/variables construct action payloads; selected successful
results bind later steps. A created conversation ID survives unrelated sidebar
selection and is used by the return step. Old revision-1 definitions remain
available, while revision-2 workflows have separate definitions. Same-revision
definition replacement, missing pointers, inherited keys, non-JSON selected
results, stale asynchronous completions and unsupported required adapters are
rejected. Profile changes preserve each view's explicit model/context selection.

Pinned LibreChat 0.8.8 and NextChat 2.16.1 profiles have source-backed layout and
composer choices, including user-only bubbles and unmodified Enter handling.
The browser suite also imports a synthetic arbitrary app/version, preserves its
exact UTF-8 source including BOM/whitespace, and exercises native graph acceptance,
receipt loading, local restoration and late-load view intent protection.
Invalid UTF-8 fails visibly rather than being silently repaired.

The source inspector distinguishes current edited text from original imported
blocks. Exported reasoning, tool inputs/results, code, document/media references
and unknown JSON remain inspectable. Tools/code are inert and Markdown media
does not trigger automatic downloads. Excluded rows and saved versions are
available through an independent per-view display control.

Native acceptance keeps the existing GraphPacket authority, closure, immutable
receipts, CAS and drift validation. The HTTP endpoint forwards the original
body to the existing ABI. Object key order no longer causes false projection
mismatches; arrays and exact numeric values remain checked, including signed
range and floating-point precision boundaries. No C ABI symbol or schema
migration is added by this extension.

## First failures and corrections

- Native acceptance exposed order-sensitive object comparison. The native
  comparator now compares object members semantically while retaining field,
  array and exact-number checks; FFI regressions cover both acceptance and
  rejection boundaries.
- An unauthorized POST returned before the HTTP library consumed its body,
  poisoning the next request on a reused connection. The auth response now
  closes that connection. [First native graph result](first-native-graph-results.json)
  retains the seven offline groups followed by the empty HTTP 400. Its
  `config_unchanged: false` is not a completed comparison: execution stopped
  before final configuration readback. The final 11-group result records the
  successful comparison.
- The initial smoke fixture attempted to update a nonexistent `label` column.
  [First smoke failure](server-smoke-auth.txt) is retained. The fixture now
  mutates the actual `canonical_key` column to verify drift detection.
- The first extended browser run did not await completion of a sidebar New
  conversation before testing workflow Return. [First browser result](first-browser-results.json)
  and [its failure log](first-browser.txt) are retained. The fixture now waits
  for the active new conversation; final 15/15 exercises the intended order.

## Reproduction

Native build: GCC 13, Debug, Ninja, vendored SQLite, OpenSSL and warnings as
errors. Final configuration enables shared library, server, tests and CLI.
Chromium headless shell 141.0.7390.37 / Playwright 1.56.1 was used. Python 3.12
requires the repository's test dependencies, including JSON Schema, requests
and cryptography. Run from the repository root:

```sh
cmake -S loom -B loom/build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_TESTS=ON -DLOOM_SHARED=ON \
  -DLOOM_BUILD_CLI=ON -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WERROR=ON
cmake --build loom/build/dev -j 1
ctest --test-dir loom/build/dev --output-on-failure -j 1
python3 -m unittest discover -s loom/tools/contracts -p 'test_*.py' -q
npm --prefix loom/web ci
npm --prefix loom/web run build
npm --prefix loom/web run test:application-profiles
npm --prefix loom/web run test:application-profile-graph
npm --prefix loom/web run test:application-profiles-browser
npm --prefix loom/web run test:imported-message-content-state
npm --prefix loom/web run test:imported-message-content-browser
npm --prefix loom/web run test:imported-message-content-native
```

The tests accept `LOOM_SERVER_BIN` and `PLAYWRIGHT_CHROMIUM_EXECUTABLE` overrides.
Graph/browser JSON reports can be retained using fresh directories supplied as
`APPLICATION_PROFILE_GRAPH_EVIDENCE_DIR` and `APPLICATION_PROFILE_EVIDENCE_DIR`.

## Explicit limits

These are partial interface/workflow mappings, not proof that GitHub contains
every app/version or that original ChatGPT/Claude/Gemini behavior is reproduced.
The [pinned source audit](../../APPLICATION_PROFILE_SOURCES.md) identifies the
observed files and departures. Specialized artifact/media/voice/browser views,
complete descendant-branch navigation and missing service operations need
renderers/adapters. Android's current JNI bridge lacks graph profile storage.

Saving a profile creates a completed canonical knowledge run. Existing automatic
latest-run queries may consequently select that profile run; explicitly pinned
context remains pinned. This actual write effect is disclosed beside Save and
in the contract. Applying/loading a profile does not itself perform that write.
Native profile definitions and source receipts survive client storage loss;
browser view layout and local workflow variables do not become cross-device
sessions. Local traces do not prove durable remote execution or rollback after
a successful side effect followed by a crash. Individually queryable capability
links and TaskEngine-backed workflow recovery remain future work.

The [earlier prototype verification](../application-profiles-2026-10-04/RESULTS.md)
is retained as a separate dated measurement.
