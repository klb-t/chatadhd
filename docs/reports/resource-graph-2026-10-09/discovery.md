# Generic discovery and declarative structural adaptation

Base: main `9e20f99a` (root workstream pins full SHA in STATE). Scope is the
owner-corrected generic resource mechanism, not Office product expansion.

## Delivered code and public API

- `loom/tools/resource_graph/discovery.py`: `Discovery`, `iter_mapping`,
  `apply_mapping`, `validate_mapping`, `export_discovery_packet`,
  `ExecutableAdapterGate`.
- `loom/data/resource_graph/discovery.json`: strategy composition, local adapter
  registry, user policy, inspection/test budgets, graph vocabulary.
- `loom/tests/resource_graph/test_discovery.py`: 24 generated-fixture mechanism
  tests; no real user data, provider/model calls, or measured LLM quality.

Reuse: `loom.tools.seeding.method_graph` supplies existing canonical JSON,
RFC 6901 lookup and data-selected walker. A small composition wrapper distinguishes
literal object key `*` from wildcard selection. Native entities, evidence sources,
claims and GraphPacket use existing `method_graph_export_v1` and
`agentic_graph_v1.packet` contracts. No graph/configuration/workflow store is added.

```python
from loom.tools.resource_graph.discovery import Discovery, iter_mapping

value = {"new_structure": [{"p": "alpha", "q": 4}, {"p": "beta", "q": 9}]}
report = Discovery().discover(value, source={"logical_id": "example", "source_version": "v1"})
adapter = report["alternatives"][0]
records = iter_mapping(value, adapter["mapping"], source=report["source"])
first = next(records)  # source selectors and fields; values stay at the source
```

`apply_mapping` explicitly materializes the iterator's selected output.
`inline=True` explicitly embeds each selected field. Neither API copies anything
into native storage. Discovery runs when asked on an already parsed value;
source attachment and deferred transport/parsing belong to root ResourceGraph.

## Demonstrated scope

A generic walk discovers record sets under arbitrary nested names, compiles their
field selectors into a reusable declarative mapping and validates actual
iteration. Renaming the fixture structure requires no provider-name branch,
renderer change, or graph-core change. Scalar, empty, heterogeneous, and
object-array structures remain physically addressable. Unknown fields arriving
later remain available by source reference; inline mode additionally retains their
values. JSON Pointer escapes and literal `*` fields are tested.

Schema and sample are retained as separate interpretations. Identity candidates
are unique scalar fields **only in the inspected sample**; they remain hypotheses,
never authoritative logical IDs. Physical selectors explicitly belong to a source
version. Available adapter, recognition, validation evidence and permission are
separate fields. No alternative is auto-selected by discovery.

Every adapter exposes `structures` with kind, selectors, fields and addressing for
renderer-free navigation. The iterator is lazy in output; the caller's JSON value
is already parsed. This is not a claim that standard JSON parsing itself streams.

## Strategies and permissions

- Local registry: JSON Schema match predicates plus data mappings; can declare a
  capability while marking its adapter unavailable.
- Declared schema: actual `jsonschema.Draft202012Validator`, explicit offline
  `referencing.Registry()`; no automatic external `$ref` fetch.
- Sample structure: generic field/record-set analysis, budgeted inspection.
- Online documentation: injected, controlled `document_fetch(url)` only, separately
  enabled policy. Fetch receives no sample. A caller-selected JSON Pointer extracts
  a declared schema from the document. URL, version, acquisition timestamp and
  byte hash are recorded; documentation text never executes or directs an agent.
  Query/fragment and userinfo URLs are rejected without echoing credentials.
- Optional model: explicitly `unavailable` until caller registers an operator.
  No model/API key is required. No paid calls were made.
- Composition: named strategies reference other registered strategies; cycles
  fail explicitly. New operators can be registered as trusted caller functions,
  not imported from untrusted document strings.

The online strategy implements retrieval of supplied schema/document URLs. It
**does not perform autonomous web search or infer schemas from arbitrary prose**.
That requires an additional search/parser operator; the current extension point
is tested with a custom operator. Complex schemas (`$ref`, `oneOf`, `anyOf`,
`allOf`, dynamic refs, pattern properties and tuple items) can be validated by the
schema library but projection explicitly reports partial recognition. Unknown
schema elements are preserved in the mapping's source schema.

## Executable adapter boundary

Default: no executable testing or activation. A test requires the exact source
SHA in `executable_test_sha256`. It runs a new Python process in a temporary working
directory with an empty inherited environment, CPU/address-space/output-file limits
and wall timeout. Actual successful execution, timeout, and credential-isolation
paths are tested. Activation requires a separate digest allowlist and a successful
receipt recorded by the same gate instance. A fabricated receipt does not activate.

This is **process/resource isolation, not a hostile-code security sandbox**: it
does not prevent filesystem/network syscalls. Therefore unknown fetched code is
not eligible. The exact code digest must already be trusted. Activation returns a
descriptor for the caller's existing operator/workflow mechanism; it creates no
second execution engine. The demonstrated test establishes JSON protocol behavior
on a generated fixture, not correctness of arbitrary adapter semantics.

## Evidence and limits

Executed 2026-10-09, Python 3.12.14, installed jsonschema 4.26.0:

```sh
python -m unittest loom.tests.resource_graph.test_discovery -v
```

Result: **24 tests passed, 0 skips** (~0.20 s). This includes real subprocess
execution for the trusted fixture adapter and exact encode/decode via the existing
GraphPacket codec. Native database persistence is verified separately by the root
workstream: passing this module's packet test alone makes no such claim.

Documentation checked before using current dependencies:

- https://python-jsonschema.readthedocs.io/en/latest/api/jsonschema/protocols/
- https://github.com/python-jsonschema/jsonschema/blob/main/docs/referencing.rst

No arbitrary JSON Schema is interpreted as a command. No external schema location
is fetched by validation. Registry policy and budgets are visible data; the
underlying environment can impose additional hard resource limits independently.

Next integration step: root ResourceGraph calls `Discovery.discover` on demand and
passes selected mappings to `iter_mapping`; end-to-end native-store evidence
should include a packet from `export_discovery_packet(report, observed_on=...)`.
