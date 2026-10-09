# E — adapter boundary and executed compatibility

Base: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` (`main`, pinned before E).
Owned implementation: `loom/web/src/graph-perspectives/adapters.ts`.
Owned verification: `loom/web/tests/graph-perspectives/adapters.test.mjs`.
The branch report records the E checkpoint SHA; this report does not claim a commit
identity for its own contents.

## Existing boundaries used

- Existing `loom.graph_packet/1` entities, claims, sources and raw record fields.
  The adapter is a read-only view over supplied packets, not a store or importer.
- Existing `content/imported-message.ts:importedPointer` for own-property RFC6901
  addressing. The adapter does not implement a second JSON-pointer parser.
- Existing `profiles/graph.ts:parseApplicationProfileSource` and
  `profiles/runtime.ts:registerProfile` for actual application-profile loading.
  Profile fields are selectable before any renderer or panel has been opened.
- Existing native GraphPacket codec `validate_packet` validates the D producer
  output and the safe fixture elsewhere in the suite. TypeScript checks only its
  read boundary; it does not claim to revalidate native hashes or receipts.
- Headless `SourceAdapter` is the single interface used by selection and UI.
  No alternate selection implementation exists in a renderer.

## API

`createPacketAdapter(packet, descriptor)` (`createGraphPacketAdapter` alias)
returns `SourceAdapter` plus `metrics` and the supplied packet reference.
`resolve(ref, context)` projects only the requested entity. On first packet access,
optional acceleration is realized as transient ID/adjacency indexes holding row
references, not copied graph records. Canonical packet rows remain unmodified.
Returned raw object/claim properties are cloned so callers cannot modify canonical
rows through a projected object's properties.

`createDemandAdapter(descriptor, loader?)` addresses an unopened source without
rendering it. The host supplies the actual read/project operation. Without a
loader, the reference remains present with `unloaded`/`unknown` status. Source
permission is checked before any loader call. Missing resources and loader errors
become `unavailable`; permission refusal remains `denied`; unsupported structures
remain explicit. Aborted reads propagate cancellation.

`createApplicationProfileAdapter({descriptor, sourceText, childRelation, registry?})`
uses the existing profile loader and pointer selector. The profile is registered
only when first requested. Data supplies child-relation vocabulary. This path
only observes fields; it does not apply settings, activate actions or send data.

All descriptors retain unknown fields. Structures and relation vocabularies are
supplied as data; none of the mechanisms assumes a spreadsheet, conversation,
profile or sensor source.

## Mapping descriptions and epistemic discipline

Native fixture metadata may supply `attrs.perspective.ref`, `status`, `evidence`,
`confidence`, and `recognition`. Claim presentation metadata uses
`qualifiers.extra.perspective`. Older flat metadata fields are also readable.

A producer need not add that metadata. A descriptor may supply `objectMapping`,
whose fields are RFC6901 pointers into a native entity, for example:

```json
{
  "objectMapping": {
    "selector": "/attrs/selector",
    "canonicalId": "/attrs/selector",
    "snapshot": "/attrs/content_sha256",
    "status": "/attrs/status",
    "recognition": "/attrs/recognition"
  },
  "availabilityMap": {
    "unsupported": "unknown",
    "corrupt": "unavailable",
    "partial": "available"
  },
  "evidenceMap": { "derived": "computed", "observed": "source" }
}
```

This is data describing field projection, not another graph schema. Original
fields and producer-specific recognition details remain available in `raw`.
Unmapped native evidence displays `unknown`. Native numeric confidence is not
silently treated as calibrated confidence: it stays raw unless explicitly
supplied through perspective metadata or a confidence field mapping. In particular,
D's parser-generated native `confidence: 1` is not displayed as confidence 1.

Identity distinguishes source+selector, canonical object, representation and
snapshot. Exact representation ambiguity is reported instead of guessed. Canonical
lookup may select another version while retaining the caller's address; the
resolved physical representation is recorded at `properties.address`.

Pagination includes total and continuation cursor. Permission-filtered edges
are explicitly reported with `status: denied`, `reason: permission_filtered`;
allowed edges can still be presented. No denied object content is returned.
The expensive cloning of relation records occurs after page selection.

## Executed verification

Command from `loom/web`:

```sh
node tests/graph-perspectives/adapters.test.mjs
```

**11/11 groups PASS**, including:

1. Lazy native projection, unknown fields, canonical immutability and no fabricated confidence.
2. Same object in different physical/logical structures without renderer state.
3. Distinct denied, unloaded, unavailable and unknown states.
4. Snapshot canonical identity with explicit representation, missing-version handling.
5. Explicit pagination, invalid budgets and unsupported structures.
6. Unopened source selection, zero reads before permission, missing/failing loader.
7. A previously unseen sensor structure through the unchanged actual headless engine.
8. Actual `registerProfile` loading of an unrendered external-profile field.
9. Data-defined evidence mapping, raw unknown preservation and malformed packet rejection.
10. A sparse 10,001-entity/10,000-claim hub with page size 3 and permission omission.
11. Actual pinned D resource production, selection, GraphPacket validation and E projection.

The large packet test resolves/materializes **0 objects and 3 relations** for its
first neighbor page, while retaining a continuation over all 10,000 edges. Local
samples of query plus lazy index were 65.09–140.58 ms. These are bounded smoke
measurements, not a cross-machine latency guarantee or layout/render benchmark.
The packet-hub test validates the packet adapter; the main benchmark separately
measures demand-source selection and presentation.

Standalone TypeScript gate:

```sh
./node_modules/.bin/tsc --noEmit --target ES2022 --module ESNext \
  --moduleResolution Bundler --skipLibCheck src/graph-perspectives/adapters.ts
```

## Actual D compatibility, after its branch became available

Producer SHA: **`1d3d133154f213733b7a69af863613cec2dd8ca2`** from
`gpt/resource-graph-2026-10-09`. A clean checkout needs this object locally:
`git fetch origin gpt/resource-graph-2026-10-09`.

The test extracts only that pinned D source/data into a temporary directory using
`git archive`. It executes D's actual `ResourceGraph`, with a newly authored safe
JSON file, not a UI mock or an E parser:

- attach performs zero source opens;
- demand selection `/instrument/samples/1` returns `8`;
- D projects the requested subtree to a native packet and its existing codec validates it;
- E resolves the same selector through the descriptor mapping above;
- E traverses incoming native claims with the same reference;
- 9 entities, 14 claims, two demand opens; no native-store import, source embedding
  or source cache; packet bytes remain unchanged by E.

No D file was edited. D's first checkpoint reports its own 54 scoped tests and
seven native-store tests; E does **not** relabel those as independently rerun.
D packet compatibility is executed here, but a persistent app transport from E to
D remains B's integration responsibility. The shipped harness must not claim
live Python-resource hydration merely because the compatibility test passes.

## B boundary and remaining scope

B inspected at `362cb8b951de23bdea989f37e67d014e62861342`. Its report describes
existing catalog resource/profile projections and open discovery/permission/cache
work. E does not modify B's runtime or install competing parsers. Demand callbacks
are host-owned. No automatic online discovery/model calls are dispatched by E.
The integration patch and browser/headless tests are maintained by the corresponding
E owners, not by this adapter report.
