# Integrator9 main documentation gate — 2026-10-04

Production source is exactly public `161cc22dfb84fe863389d6b90323bd44516a68dc`.
Tested docs checkpoint: `bda9ef95d14a90fc5e5e5df2ca51469460a07c35`.
Final changes after execution are documentation only. No held lane source,
README, profile, test or repository build configuration is changed.

Fresh bundled-SQLite/server dev build and web build pass (85 web modules).
The complete serial CTest passes **108/108 in351.36s**. Original output/XML
passed the unchanged actual-case guard: **107 executed entries**, **659 native
cases /24465 assertions**, **1276 Python /0 skipped**. Context and knowledge
each execute18 cases. Existing opt-in catalog_scale returns0/0 and remains
explicitly unexecuted; it is not counted as case coverage. All108 outer entries
are present. No assertion/timeout/quality threshold was removed or loosened.

[evidence.zip](evidence.zip) contains complete JUnit, manifest, logs, guard,
counts and build receipt. `SHA256.json` hashes every payload. The actual126
thin-archive member objects exactly match CMake/Ninja's input set and are hashed,
as are all tested native binaries. No W2 usage-policy object remains in this
baseline archive. Low-memory linker/debug/archive flags are local build options;
production sources and assertions are unchanged. No paid provider calls.

Replay from the public production source with the recorded dev runtime/options:

```sh
ctest --test-dir loom/build/dev --output-on-failure -j1 --no-tests=error --test-output-size-passed 10485760 --test-output-size-failed 10485760 --output-junit /tmp/replay.xml
ctest --test-dir loom/build/dev --show-only=json-v1 > /tmp/replay-manifest.json
python3 verify_ctest.py --preset dev --manifest /tmp/replay-manifest.json --junit /tmp/replay.xml --output /tmp/replay-cases.json
```

This is the main documentation gate, not the separately archived W2 gate.
It does not claim W8's full CI matrix is green: vendored Clang still requires
its scoped server-capture fix. Earlier negative runs remain in their archives.
