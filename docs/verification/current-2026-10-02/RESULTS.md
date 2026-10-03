# Current verification — 2026-10-02

The selected source passes **107/107 CTest suite entries**, zero failures, in
**77.58 seconds**, with external `PYTHONPATH` and `TMPDIR` unset. Tests ran
from a tracked-only source snapshot using only reachable sanitized Git
objects. The original W3 base commit was absent; its mapped commit and exact
unchanged source tree were verified instead.

| Gate | Result |
|---|---|
| C++20 Debug, warnings as errors, bundled SQLite, shared library, CLI/server | Passed |
| Complete CTest | 107/107; 77.58 s |
| Structure entry | 859 individual tests, including six new Git provenance regressions |
| TypeScript/Vite production build | Passed |
| Mock transport and retrieval-plan state | Passed |
| Browser-local workspace state | 8/8 groups |
| Browser to actual native workspace | 10/10 scenarios |
| Browser chat/context to native server and local fake provider | Passed; 5 local fake-provider calls |
| General browser/native end-to-end | 16/16 steps |
| README synthetic Knowledge command | Completed successfully, no API key/model call |
| DEMO synthetic Archive command | Completed successfully; named report outputs verified |
| Local Git privacy reconstruction | Separate derivative: 404 historical trees/topologies preserved; private contact absent from its reachable commit metadata |
| Private restoration utility | Six local safety checks passed |
| Offline Knowledge browser demo | 60 graph nodes, 174 claims; zero external requests/page errors |

These establish engineering behavior on the recorded source. They do not prove
arbitrary-archive extraction quality, model efficacy, Android packaging or
real-device behavior. There were **zero external model calls** and no GitHub
Actions runs. Debug flags were `-O0 -g0`; assertions and warnings-as-errors
remain enabled. Real-provider answer quality was not measured.

## Correction exercised by this verification

A source-only tar has no Git object database. Its first CTest attempt passed
106/107 and failed W3's historical-byte provenance check. A subsequent check
also detected dangling original commits in the intermediate sanitizer mirror;
that cancelled attempt was not accepted as a sanitized-only result.

The final snapshot uses a reachable-refs-only Git clone where the original
base SHA is absent. W3 now resolves a unique original→sanitized map entry,
checks its declared and actual tree against the pinned original tree, then
checks exact source bytes. Six real-Git regressions cover correct mapping,
missing/ambiguous mapping, declared/actual tree mismatch and changed bytes.
`resolved_commit` and `git_tree_sha` are optional additive JSON source-record
fields; original provenance is retained. No database/ABI migration or
quality-gate relaxation was introduced.

## Reproduce

The integration published on 2026-10-03 retains original public main ancestry.
The measured code/support files are unchanged: the manifest below pins all 898
files. Metadata-corrected lineage was exercised by the recorded verification;
the public repository retains original historical identities, including W3's
original base. The private-contact finding in its old ancestry is unchanged.

Use a Git clone of the hosted repository or the separately prepared repository
bundle, rather than a source-only ZIP, so archived source objects remain
available to the provenance checks. Fetch the historical source refs when
using a single-branch clone of the hosted repository.
Follow the root README for CMake 3.25+, dependencies, the complete tests and
both isolated fictional-data demonstrations. For the measured Debug settings:

```bash
cmake --preset dev -S loom -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' -DCMAKE_C_FLAGS_DEBUG='-O0 -g0'
cmake --build loom/build/dev -j 4
env -u PYTHONPATH -u TMPDIR ctest --test-dir loom/build/dev --output-on-failure -j 4
```

The [source manifest](source-final.json) pins all 898 measured code/support files.
The [receipt](receipt.json) records source/binary identities and measured
commands. Detailed first-attempt and final logs remain in the accompanying
verification archive. The older [handoff receipt](../2026-10-02/RESULTS.md)
remains a separate historical measurement.
