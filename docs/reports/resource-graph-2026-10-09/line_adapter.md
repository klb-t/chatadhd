# Bounded reference adapter: JSON Lines

Base: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`.
This is a structural-access demonstration on generated parser fixtures, not
user conversation data or an LLM quality measurement. No source is modified.

`loom.tools.resource_graph.line_adapter.JsonLinesAdapter` plugs into the
resource registry through `register_adapter(id, adapter)`. The graph core has
no JSON Lines branch. Its domain semantics remain **unknown**; syntactic JSON
fields are nonetheless addressable and project through `loom.graph_packet/1`.

## Actual execution

From repository root:

```sh
python -m unittest discover -s loom/tests/resource_graph -p test_line_adapter.py -v
```

Measured 2026-10-09: **29 tests pass**, including the real ResourceGraph
registry, selection, page and packet projection paths. A generated source of
**33,184,000 bytes / 32,000 rows** yields a requested two-row page after
**8,192 actual source bytes read**, including all buffered read-ahead. Only two
lines are parsed. The exact total row count and whole-source hash remain
unknown. Open, describe and depth-zero virtual-root projection read **zero
content bytes**. No native store, renderer or whole-source materialization is
used in this measurement. Other suite reports cover native persistence.

Minimal use:

```python
from loom.tools.resource_graph import ResourceGraph
from loom.tools.resource_graph.access import Access
from loom.tools.resource_graph.line_adapter import JsonLinesAdapter

graph = ResourceGraph(access=Access(policy={"local_roots": ["/authorized"]}))
graph.register_adapter("my-line-structure", JsonLinesAdapter())
graph.attach("/authorized/source.jsonl", logical_id="source:chosen-by-caller",
             adapter="my-line-structure", policy={"mode": "live"})
value = graph.select("source:chosen-by-caller", "/5/unknown_field/nested/0")
page = graph.children("source:chosen-by-caller", limit=3)
packet = graph.project("source:chosen-by-caller", "/5/unknown_field", depth=1)
```

The first selector segment is a **zero-based physical line** at the observed
source revision, followed by RFC 6901 object/array addressing. It is not a
logical record identity. Whole-source `select("")` returns a descriptor rather
than an eagerly constructed list. `node("")` exposes a virtual sequence for
core graph projection. Fields, including unknown fields, use their original
keys; escaping `~` and `/` is tested.

## Composition and evidence

- Access permission uses the shared `Access.authorize_local` without a source
  read. Standalone use requires explicit `allowed_roots`. Read permission and
  roots are rechecked on operations. Parent-directory and final-file opens
  reject replacement symlinks, and nonregular sources cannot block the reader.
- Local seeking and metered buffering are separate from `parse_json_line`, a
  reusable one-line syntax operator using the standard Python JSON decoder.
- Source version is explicitly a filesystem-stat observation (device, inode,
  size, mtime and ctime), not a content hash, URL or logical identity.
- Live `refresh()` adopts a current revision and invalidates offsets; snapshot
  mode rejects changed bytes when no snapshot copy was retained. It does not
  claim an old source can be reconstructed. `metadata()` checks revision
  consistency without silently adopting a new revision. Read operations also
  check stat before and after reading, preventing mixed-revision success.
- Cache is optional and contains sparse byte offsets only. Its stride and
  maximum entries are independent of source embedding and native storage.
- Status is `unloaded` until demanded, `empty` only for a zero-byte source,
  `partial` for requested coverage or exhausted budgets, `unavailable` for
  missing/changed/unauthorized sources, `corrupt` for invalid requested JSON,
  and `unsupported` for an unavailable addressing/access/numeric capability.
  Untouched later lines may be invalid without making an earlier valid line
  unreadable. This never claims complete validation of the source.
- Duplicate object keys are rejected rather than silently losing a value.
  Non-JSON constants and invalid UTF-8 are rejected; numeric float overflow
  is explicitly unsupported rather than becoming infinity in the graph.
- Source metadata and diagnostics contain no file content. Error codes do not
  interpolate source exception messages or credentials. Remote URLs and ZIP
  members require other transport/container operators; this local seekable
  adapter never fetches them implicitly.

## Budgets and declared limits

Caller policy defaults live in `loom/data/resource_graph/line_adapter.json`.
Maximum line bytes, scanned bytes per request, returned page items, sparse
index entries and read buffer are configurable. Optional host-supplied
`environment_limits` are a separate constructor input. Effective byte/page
limits use the lower bound; a budget exception identifies `user_policy` or
`environment_limits`. This adapter does not invent or infer environment
ceilings. Source size itself does not force a complete read.

This implementation supports secure local descriptor traversal on POSIX;
an environment without the required safe-open primitives is explicitly
unsupported. Selecting a distant line without an offset index still requires
scanning preceding bytes, subject to the request budget. Cached offsets can
reduce later scans; they are not a promise of arbitrary O(1) seeking. A selected
line is parsed in full within its line budget; inner-field parsing is not a
streaming JSON query engine. No schema/domain inference, attachment semantics,
logical-record resolution, write-back or remote range protocol is claimed.

## Checked documentation

Documentation was consulted on 2026-10-09; the code uses Python APIs available
under the repository's Python 3.11+ contract:

- [Python I/O, raw streams and bounded buffering](https://docs.python.org/3/library/io.html)
- [Python JSON decoder and object-pairs hook](https://docs.python.org/3/library/json.html)
- [RFC 6901 JSON Pointer](https://www.rfc-editor.org/info/rfc6901/)

The repository's existing conversation importer is a domain import consumer;
it is not reused to emulate bounded random structure access because it imports
messages and would introduce unrelated persistence/domain behavior. This
adapter instead reuses the standard syntax parser and the shared access,
adapter, projection and packet contracts.
