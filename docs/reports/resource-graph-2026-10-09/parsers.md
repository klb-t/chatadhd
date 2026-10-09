# Syntax and existing provider mapping — 2026-10-09

Base: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. No existing importer,
`loom/tools/structure`, graph schema, native implementation or build file changed.
The owner's correction is authoritative: formats are interchangeable examples of
the generic access/parser/adapter mechanism. No Office implementation was added.

## Reuse and delivered API

`loom.tools.resource_graph.parsers.parse(bytes, format_id, options=...)` returns
an addressable syntax tree. `register_parser`, `parser_descriptor`,
`available_parsers`, and `recognize` provide a registry rather than provider-name
dispatch. Recognition reports all requested syntax alternatives and never
claims semantic interpretation; JSON can also parse as YAML or CSV.

Existing libraries are used, not parser copies:

| Syntax | Implementation | Preserved structure | Explicit boundary |
|---|---|---|---|
| JSON | Python `json` | objects, arrays, unknown fields, scalar types | duplicate members and nonfinite numbers rejected explicitly; formatting stays at original source |
| YAML | PyYAML `SafeLoader` node/event API | mapping/sequence/scalars; unknown tags; explicit alias pointers, including cycles; non-string mapping keys as ordered entries | comments/lexical formatting remain only at source; merge keys are syntax and are not silently resolved as configuration |
| XML | defusedxml + standard ElementTree | expanded names, attributes, text/tail, children, comments and processing instructions within the root | namespace prefixes/lexical formatting/prolog comments remain at source; DTD/entity/external fetch forbidden; XInclude stays ordinary data |
| CSV | Python `csv.reader` | all rows, duplicate/empty fields, quoted embedded newlines | no inferred header, domain type or formula execution |

The parser consumes the requested document/member on demand; it does not claim
streaming random access inside every syntax. Graph node materialization is a
separate caller operation. Byte/depth/node/alias budgets and delimiter/encoding
are caller options; defaults live in `loom/data/resource_graph/parsers.json`.
Environment transport/container boundaries are separately enforced by `Access`.

## Native mapping bridge, deliberately separate from reference syntax

The fidelity-preserving OpenAI/Anthropic mapper already exists natively.
`engine.importer` is a legacy flattening importer and was not used as a false
fidelity substitute. `catalog.read_unit` exposes source bytes but not the full
provider-domain mapper. Pure native parser helpers are declared only in
`src/import/export_internal.h`, explicitly not a public API.

`NativeConversationMapper` therefore reuses public `loom_import_file_ex`
(`export_mode:on`), `loom_get_messages_ex` (`include_all=1`) and the existing
`NativeGraphStore` lifecycle (workers disabled). It has two explicit modes:

- `import_path(..., data_directory=...)`: owner-selected native import retained
  in the specified existing native store.
- `map_bytes(...)`: optional eager compatibility mapping into a **temporary
  native store**, removed after reading results. Its receipt says
  `native_temporary_materialization`, never lazy/no-materialization.

The main `ResourceGraph` reference/syntax route does not invoke this bridge,
requires no native library or database, and exposes message/branch/author
fields directly by selector. The bridge has no replacement mapper and no
alternate database. A public nonmaterializing provider-domain API remains an
integration gap for B; it is not required to attach/read/query the source.

The optional eager bridge applies the same recursive `Access.validate_archive`
preflight used elsewhere before native ZIP import. It stages a private byte
snapshot because the existing public import ABI lacks a source-hash admission
argument; this prevents source replacement between preflight and native read.
That copy is disclosed in execution metadata. It never alters the original.

Results retain full native rows and reports. A separate comparison view replaces
generated DB IDs with source keys; excludes native attachment filesystem paths,
generated conversation/import timestamps and optional member source/blob/locator
bookkeeping; and declares those representation changes. Provider raw metadata,
parent bindings, roles/authors, source timestamps, branch/version membership,
available asset member references and unresolved references are compared.
Temporary native attachment paths expire; original archive/member selectors
remain the usable source location.

## Executed checks

Runtime: Python 3.12.14, PyYAML 6.0.3, defusedxml 0.7.1. defusedxml was absent,
then ordinary `python -m pip install defusedxml PyYAML` succeeded. No paid calls,
online sample uploads, private inputs, or application worker/model calls.

```sh
LOOM_LIBRARY=/absolute/path/to/libloom.so python -m unittest \
  loom.tests.resource_graph.test_parsers \
  loom.tests.resource_graph.test_conversations -v
```

Final run: **21/21 PASS, 0 skips, 5.608 s** (15 syntax/recognition tests and 6
conversation tests, 5 of which execute the native bridge). Native library built
from unchanged base, SHA256
`3d82f67e43bd426d2f7e2faf91a9d957793ce8ed14b0b3a756ed0e54a5fee148`.
The first combined run was 20/21: nested native report bookkeeping differed
between provenance-enabled and provenance-disabled import. The comparison view
was corrected to declare/remove those storage representation fields recursively;
raw results remain untouched. Nested rerun and final combined rerun passed.

Tests use the repository's existing **generated synthetic** export fixtures;
these are not owner conversations or a model-quality experiment. Native checks
cover OpenAI sharded ZIP, Anthropic export, ZIP inside ZIP, full durable import
vs acquired-reference bytes, controlled injected remote vs local bytes, source
immutability, actual native rows, branch bindings, raw author metadata, available
attachments and unresolved references. Unsafe/truncated ZIPs are refused before
native consumption. The independent reference test reads conversation author,
branch and message content through `ResourceGraph` with no native store.

## Documentation checked as data

Official documentation retrieved 2026-10-09:

- [Python JSON](https://docs.python.org/3/library/json.html): decoder hooks,
  duplicate/nonfinite handling and input-size concerns.
- [Python CSV](https://docs.python.org/3/library/csv.html): `reader`, newline
  handling, string-valued fields and caller-selectable dialect.
- [PyYAML](https://pyyaml.org/wiki/PyYAMLDocumentation): safe loader,
  compose/events/nodes, anchors and aliases. Tags never execute Python objects.
- [defusedxml](https://pypi.org/project/defusedxml/): `forbid_dtd`,
  `forbid_entities`, `forbid_external`.
- [ElementTree](https://docs.python.org/3/library/xml.etree.elementtree.html):
  expanded names, text/tail and `TreeBuilder` comment/PI retention.

Documentation is evidence about parser APIs, not instructions to execute
source-provided code. No document content is treated as agent authority.
