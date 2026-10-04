# Application profile verification — 2026-10-04

Base public main: `421415f8b9a5c29e25fc4c5fda5b87ba8f5fb9cd`.
Contract checkpoint: hosted `7006b3ae81ce5c40fc8ac62ca34ba01a81405c4c`
(same tree as local `e51614f`, tree `d06045805f28e3bfd97b4cdf1d548886af043f0e`).
The following checks apply to the profile integration source, fingerprinted in
[`source-sha256.json`](source-sha256.json). Native C++ source, core/KB schema and
C ABI were not changed. Original 108/108 CTest results are not reassigned to this
snapshot; the native test executable was not built for this session.

| Check | Measured result |
|---|---|
| `npm run build` | Strict TypeScript and Vite production build pass, 64 modules |
| `npm run test:application-profiles` | Runtime 14/14 groups; API adapter 9/9 groups; offline |
| Python contract discovery, `test_*.py` | 205/205 tests pass, including 8 new application profile contract tests |
| `npm run test:workspace-state` | 8/8 groups pass |
| `npm run test:context-plan` | Retrieval plan regression passes |
| `npm run test:transport` | Native transport contract regression passes |
| `npm run test:chat-context` | Browser/native context regression passes; 5 local fake-provider calls, zero remote |
| `npm run test:application-profiles-browser` | 12/12 groups pass; actual native server/Chromium; 6 local fake-provider calls, zero remote |
| Existing `npm run e2e` | 16/16 steps pass against actual native server; [retained log](previous-browser-suite.log) |
| Native server smoke | Knowledge/catalog/context, general smoke and authentication checks pass |
| `test_chat_active_task_server.py` | 1/1 scenario pass; 5 local provider calls, zero remote; full server restart |
| `git diff --check` | Pass |

The dedicated profile browser test proves:

- UI changes preserve explicit model, knowledge query and context source flags;
  native/provider request captures retain the selected model/context.
- Two simultaneous profiles keep independent controls and share the selected
  conversation. Native message edit/version restore refresh both views.
- A custom arbitrary application/version profile imports as JSON; right sidebar
  geometry and `mod-enter` work. Unsupported required adapters block activation;
  missing/absent Send disables submission and preserves the draft.
- Declarative new-conversation → knowledge → return transitions invoke actual
  API/UI operations; exact profile definitions and successful workflow states
  restore after reload without changing server/provider configuration.
- Completion for conversation A does not replace selected conversation B;
  delayed old cancellation does not clear a new send; truncated EOF clears
  pending content and exposes failure.

[`native-browser-results.json`](native-browser-results.json) retains synthetic
browser requests, captured local-provider requests, native mutations, view and
workflow snapshots, all 12 group names, no page errors and unchanged configuration.
The EOF case explicitly injects a synthetic transport fixture in the browser;
the six provider calls go through native Loom. Synthetic data tests mechanism,
not original-app fidelity or model quality. No original ChatGPT/Claude/Gemini
service, actual user export, device, paid model or cloud VM was involved.

## First failures retained and corrected

1. The first new schema used optional `uri` format validation, unavailable with
   the repository's documented contract dependencies. Whole-contract discovery
   initially failed (123 tests reached; 5 failures, 44 errors). The new schema
   now uses a URL pattern plus normative runtime URL parsing, without changing
   existing gates or dependencies. New tests load all existing schemas with
   URI/IRI checkers deliberately unavailable; final discovery passes 205/205.
2. Native linking encountered damaged generated object files (zero-length or
   missing ELF headers). Only generated objects were regenerated; final
   sequential rebuild linked the existing native source successfully. No kernel
   source fix or quality-gate relaxation was used.
3. The initial profile browser run passed 3 groups and failed on a test locator
   that disappeared when an editable message body became a textarea. The script
   uses a stable message-row locator now. [First run](first-browser-run.json)
   remains separate; the final result is 12/12. Product code was not changed to
   accommodate that selector.

Native build used GCC 13, vendored SQLite, OpenSSL, Debug and warnings-as-errors:

```sh
cmake -S loom -B loom/build/dev -G Ninja \
  -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_TESTS=OFF \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WERROR=ON
cmake --build loom/build/dev --target loom-server -j 1
python3 -m unittest discover -s loom/tools/contracts -p 'test_*.py' -q
npm --prefix loom/web ci
npm --prefix loom/web run build
npm --prefix loom/web run test:application-profiles
npm --prefix loom/web run test:application-profiles-browser
```

Chromium headless shell 141.0.7390.37 / Playwright build 1194 was used. The
test accepts `PLAYWRIGHT_CHROMIUM_EXECUTABLE` and `LOOM_SERVER_BIN` overrides.
GitHub Actions were skipped; no Actions budget was consumed by these checks.
