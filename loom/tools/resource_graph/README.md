# Resource graph extension

A composable, headless source-reference path into the **existing** Loom GraphPacket.
Attach any locator before access or recognition. Request only the needed structure;
no native store or renderer is required. This is not a graph/config/workflow engine.

From the repository root:

```bash
python -m pip install -r loom/tools/resource_graph/requirements.txt
python -m loom.tools.resource_graph describe /path/to/source --id source-1
python -m loom.tools.resource_graph project /path/to/profile.json --selector /settings --depth 2
python -m loom.tools.resource_graph project /path/to/archive.zip --member nested.zip --member profile.json
python -m loom.tools.resource_graph recognize /path/to/unknown.extension
python -m loom.tools.resource_graph demo --output /tmp/resource-graph-demo
python -m unittest discover -s loom/tests/resource_graph -v
```

Native tests require an actual library. Without it, native-only tests explicitly
skip and **do not** establish native execution. Build instructions and measured
native evidence are in `docs/reports/resource-graph-2026-10-09/native.md`.

```bash
LOOM_LIBRARY=/absolute/path/libloom.so python -m unittest discover -s loom/tests/resource_graph -v
python -m loom.tools.resource_graph demo --output /tmp/resource-graph-native-demo --library /absolute/path/libloom.so
```

## Composition

```python
from loom.tools.resource_graph import ResourceGraph
from loom.tools.resource_graph.access import Access
from loom.tools.resource_graph.line_adapter import JsonLinesAdapter

graph = ResourceGraph(access=Access({'local_roots': ['/approved/data']}))
graph.attach('/approved/data/export.zip', logical_id='owner-chosen-id',
             members=['nested.zip', 'conversations.json'])
reference = graph.reference_packet('owner-chosen-id')  # zero source reads
field = graph.select('owner-chosen-id', '/0/title')
packet = graph.project('owner-chosen-id', '/0', depth=2, limit=100)

graph.register_adapter('json-lines', JsonLinesAdapter())
graph.attach('/approved/data/records.jsonl', logical_id='large-source', adapter='json-lines')
page = graph.children('large-source', limit=2)  # bounded source read, not whole file
```

Transport/access, ZIP container selection, syntax parsing, domain mapping, and
GraphPacket projection are independent operators. `register_adapter` accepts trusted
code already supplied by the caller; parser and discovery registries are independently
extensible. `MappingAdapter(SyntaxAdapter(), mapping)` adds a structural perspective
using a validated declarative mapping. Its output retains original selectors and
explicit unknown fields. Mapping results are derived; their virtual selectors are
never presented as observations at fabricated original-source locations.

Adapter handle protocol: `metadata()`, `select(pointer)`, `children(pointer,offset,limit)`;
optional `node(pointer)` describes a virtual container without materializing it,
`refresh()` revalidates a live handle, and `close()` releases resources. New adapters
need no graph schema or renderer change. See the JSONL example and tested discovery
registries for actual extensions.

## Identity, policy and provenance

Logical resource ID, locator/member selector, observed source version, content hash,
parser version, mapping version and permissions remain distinct. A source may have
an unknown hash (JSONL demand access), while its observed local stat revision is known.
History contains only this session's own dated observations, never invented edits.

`policy` selects `mode` (`live` / `snapshot`), `embedding`, `cache`,
`retention_seconds`, `index` and projection budgets independently. Presets live in
`loom/data/resource_graph`. Separate deployment safety ceilings are in access policy;
user overrides cannot raise those ceilings. Parser options are caller data. Changes
in a requested live source invalidate its fragment index. Index coverage is always
explicit: only requested fragments, not a search promise over unread content.

A snapshot without retained bytes fails if the observed source changes. `capture()`
explicitly retains bytes; snapshot+embedding can work after source removal. Live+
embedding still reads the original on demand and retains each successful version's
bytes. Parsed cache may serve its captured revision until the explicit TTL expires;
its version does not masquerade as freshly fetched. `evict()` removes cache/index.
`export_reference()` and `restore_reference()` roundtrip a descriptor with zero
source reads; embedded bytes are opt-in and hash-checked. No source writes occur.
`overlay()` produces only a proposal; write-back is explicitly unsupported.

`unloaded`, `empty`, `unavailable`, `unsupported`, `partial`, `corrupt` and `available`
remain distinct. Source failure never becomes an empty successful collection.

## Actual boundaries

- Standard JSON/YAML/XML/CSV parsers load the requested bounded member/document.
  They do not stream arbitrary JSON pointers. ZIP/HTTP currently obtain a bounded
  whole outer source. JSONL proves actual partial byte reads through the same API.
- Recognition is syntax/structure evidence, not semantic understanding. Alternatives
  are preserved and selection is explicit. Discovery can consume caller-supplied
  online documentation via controlled transport; general web search and models are
  extension points and were not called. No private samples are sent online.
- Found executable code is never run automatically. Tests require a trusted digest;
  activation has separate policy. A resource-limited process is **not** a sandbox
  for hostile code.
- Existing native OpenAI/Anthropic mapper reuse is an optional, explicitly eager
  temporary-materialization bridge (`NativeConversationMapper`); core references
  never invoke it. The pure public mapping seam for B is delivered as a separate
  minimal patch. Ordinary application chat/catalog dispatch is not connected here.
- The CLI profile scenario executes existing `engine.config.Config.get` against
  the same source and proves bytes unchanged; it does not activate a native runtime.
  B's separately implemented runtime-profile consumer is not credited as D's test.
- XLSX/DOCX/PPTX expansion is intentionally not pursued after the owner's correction.
  This extension supplies generic access/projection mechanisms and interchangeable
  verification formats, not an Office roadmap.
