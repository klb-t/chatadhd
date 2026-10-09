# Native external-resource conformance

`driver.cpp` only adapts JSON to actual native consumers. `run.py` generates synthetic ChatGPT JSON, a flat ZIP and local profile overlays. Every call runs a new process. Runtime workers are disabled and injected HTTP transport rejects every send. No provider credentials, private sources or remote model calls are needed.

Build a real native core from the requested SHA as described in `../../pass2/native/README.md`, then run:

```sh
python3 tools/ecosystem-audit-2026-10-09/pass3-resources/native/run.py \
  --repo CHECKOUT --sha SHA --build-dir REAL_CMAKE_BUILD --output OUT
```

The directory must contain the real `libloom_core.a`, SQLite/miniz libraries and CMake cache. Rebuild after product changes. Source SHA checks and library hashes expose provenance; they cannot make a stale manually supplied archive fresh.

The optional `--build-manifest FILE` supports the separately recorded B2 assembly from `pass2/watchdog/B2/object-manifest.json`: source/object hashes are checked, the six objects precede the B archive, and the link-map guard rejects replaced old members. The recorded absolute object paths apply only to this workspace. On another machine, use a fresh full B2 build or reproduce WD's documented selective build first. Do not label an unverified B archive B2.

Results:

- `receipt.json`: actual requests/responses, separate reproduction/acceptance/behavior/blocked criteria, archive hash, compile command and optional B2 manifest.
- `synthetic-conversations.json`: public-safe source fixture.
- `link-closure.txt`: selected archive members from the real linker.
- `compile.log`: compiler output.

Exit 1 means at least one failed acceptance or blocked required contract. The current main/B2 result is 16 PASS, 2 FAIL, 3 BLOCKED. A passed reproduction is not product conformance. There is no substitute resolver in the harness; missing graph-resource interfaces stay BLOCKED.

The copy/link comparison uses the same exact ZIP within each run. Across runs the ZIP container timestamp can differ; its hash is recorded. The conversation fixture and all asserted semantics remain identical. Actual parent references are compared by their synthetic message text to avoid incidental native-generated IDs. No semantics of parsing or source resolution are reimplemented.
