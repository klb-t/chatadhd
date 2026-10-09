# Native boundary and optional B integration — 2026-10-09

Base: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` (`main`, pinned).
B inspected at `b20c0d8ac37934e1600c3b9216492fd524220941`.
No existing source, schema, root build file or B branch was edited.

## Reused mechanisms

`loom.tools.structure.agentic_graph_v1.packet` performs packet creation,
validation and record identity. `loom.tools.coordination.graph_store.NativeGraphStore`
provides the existing C ABI wrapper. Its accept/read/replay methods use
`loom_graph_packet_store`, `GraphPacketStore` and the existing native
`KnowledgeStore`/SQLite schema. There is no resource-specific database, FFI
implementation, graph schema, or configuration engine.

`loom.tools.resource_graph.native.roundtrip(packet, library, data_dir)` validates
the packet, accepts a complete closed entity/claim/source selection, shuts down
the native context, reopens it and verifies the complete immutable receipt and
row-drift status through read/replay. The target includes the packet version,
so another packet version retains the preceding native run. An exact retry
returns its existing receipt. Native errors propagate; schema validation alone
never produces `native_executed=true`.

This function explicitly persists the selected projection. It is not required
for lazy resource access and does not promise that source bytes are in the
database. The packet determines its retained values and references. Native
initialization follows the existing data-directory loader with workers disabled;
tests exclusively use new temporary directories with no credentials.

## Executed evidence

- First configure: `cmake: command not found`. Ordinary dependency installation
  (`python -m pip install cmake ninja jsonschema`) succeeded after one PyPI
  timeout/retry. Installed CMake 4.4.4, Ninja 1.13.2, jsonschema 4.26.0.
- Actual shared native target compiled successfully with GCC 13.3, Debug,
  `LOOM_WERROR=ON`, vendored SQLite, OpenSSL. No native implementation changes.
- Shared binary SHA-256:
  `3d82f67e43bd426d2f7e2faf91a9d957793ce8ed14b0b3a756ed0e54a5fee148`.
- `test_native.py`: **7/7 PASS, 0 skipped, 7.589 s**, including actual source
  reference → on-demand structural projection → native accept → close/reopen →
  removal of source → exact receipt replay. Also covers UTF-8/unknown attrs,
  packet version retention, idempotency, invalid packet before FFI,
  missing library and deleted native row drift without repair.
- Optional hook patch applies to both exact base SHAs above. Its actual CTest
  registration executed with the real shared binary: **1/1 PASS, 7.32 s**.
  This verifies registration and its then-present test discovery; later-added
  D tests require the final combined run recorded by D's integrator.
- A preliminary run before the shared library existed executed two validation
  tests and skipped four native tests. That preparatory run is not native
  execution evidence; the subsequent seven-test run above has no skips.

Reproduce from repository root (use installed tool paths if absent from PATH):

```bash
cmake -S loom -B ../build-resource-graph -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_BUILD_TESTS=OFF -DLOOM_BUILD_CLI=OFF -DLOOM_BUILD_SERVER=OFF \
  -DLOOM_SHARED=ON -DLOOM_WERROR=ON
cmake --build ../build-resource-graph --target loom -j 4
LOOM_LIBRARY="$PWD/../build-resource-graph/libloom.so" \
  python -m unittest discover -s loom/tests/resource_graph -p test_native.py -v
python docs/reports/resource-graph-2026-10-09/integration_test.py \
  --library "$PWD/../build-resource-graph/libloom.so"
```

This session's build directory is
`/workspace/scratch/7d1c36ce781a/build-resource-graph`; complete compiler log is
`/workspace/scratch/7d1c36ce781a/resource-graph-native-build.log` (reproducible
temporary build evidence, not a required source artifact).

## Integration boundary for B

`integration.patch` is an optional **test-registration hook**, pinned to both
source SHAs above, not a runtime integration claim. It registers all D tests
only for `LOOM_SHARED`, passes the actual `LOOM_LIBRARY`, and retains a normal
600-second hang timeout. `integration_test.py` applies it solely inside temporary
directories and executes exactly its added CTest block with an imported actual
shared target. It never modifies the checked-out root build.

The library's packet-to-native path already uses a public API and needs no new
production hook. B's separate C++ `Catalog::read_resource(unit_id)` and
`RuntimeProfile` external-overlay path were inspected in B's report/contract;
their runtime tests are B's evidence, not freshly established by D. D does not
insert a Python subprocess into B's catalog or claim that ordinary chat now
hydrates every resource reference. Passing the existing packet to the unchanged
native API is the demonstrated seam. Automatic B catalog-to-D adapter dispatch,
UI activation, ASan and a full repository matrix remain outside this measured
native slice.
