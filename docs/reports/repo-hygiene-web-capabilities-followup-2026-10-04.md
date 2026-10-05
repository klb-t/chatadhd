# W8 appendix — optional W10 web checks and Clang handoff

Prepared a generic npm-script capability helper in `.github/scripts/` and added
the two W10 script names to the existing web workflow after Playwright setup.
The helper reads the checked-out package, reports absent scripts as unavailable,
executes each declared script through argv and propagates its failing exit code.
Existing web checks and product gates remain intact. Web/server sources were
not edited; this appendix does not admit W10 implementation onto main.

Before: CI ran neither new W10 script even if a future source declared them.
After: CI requests both `test:interface-2` and `test:interface-2-native` from the
actual package. On the current source both are absent, so **0 W10 scripts ran**.
Offline helper process tests: **6/6**; one real npm/local Node fixture also ran.
[Exact evidence and source hashes](../verification/repo-hygiene-followup-2026-10-04/web-capabilities/README.md).
No paid calls, remote CI starts, commits or pushes were made by this subtask.

The workflow also includes the evidence policy/schema JSON pattern in both event
path filters, and passes an explicit CMake cache path plus configured field names
to the separately maintained build receipt. Only requested configuration values
will be retained; full cache contents are not copied into this proof.

## Do wątku 10 — compiler fixes before W8's next CI

The two compiled failures retained from runs `37210513736` and `37211656572`
are unused `this` captures under Clang `-Werror` in `loom/server/src/app.cpp`:

| Source | `/api/logs` | `/api/version` | Evidence |
|---|---:|---:|---|
| Main implementation `161cc22`; unchanged through `7282437` | 768 | 772 | Actual compiler errors in both CI runs; intervening main changes only docs. |
| W10 tip `2f25145` | 835 | 839 | Source inspection: the same unused captures remain; not a compiled W10 result in this subtask. |

Smallest fix for these handlers: use `[]` in place of `[this]`. Neither handler
reads instance state. Other non-packet server routes are assigned to W10 by
the published thread index; W8 does not edit them or suppress the warning.

There is a separate **source-inferred** W10 warning risk at line **515**:
`/api/usage-policy` captures `this`, while its only use is inside
`#ifdef LOOM_SERVER_HAS_USAGE_POLICY`. W10 `2f25145` contains no
`loom/include/loom/usage_policy.h`; in the header-absent fallback the capture
becomes unused. W10 should verify Clang builds with and without the W2 header
and preserve the explicit unavailable response. This subtask did not compile
that branch and does not label this third warning as an observed CI failure.

Vendored configuration stays Clang + vendored SQLite + shared library with
`WERROR=ON`. The whole matrix remains pending the server owner's fix and fresh
verification; prior dev/ASan success does not establish vendored coverage.
