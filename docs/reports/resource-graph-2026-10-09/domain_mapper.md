# Optional B production hook: DB-free existing provider mapping

`domain_mapper.patch` is a concrete **optional production API patch**, separate
from the CTest registration patch. It is not applied to D's checked-out source.
It applies to pinned main `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` and pinned B
`b20c0d8ac37934e1600c3b9216492fd524220941`.

## API and implementation boundary

The new public C++ header `loom/export_mapping.h` declares:

```cpp
loom::Result<loom::Json> loom::map_openai_export_conversation(
    const loom::Json& conversation, const std::string& member = "", int index = 0);
loom::Result<loom::Json> loom::map_anthropic_export_conversation(
    const loom::Json& conversation, const std::string& member = "", int index = 0);
```

Each receives one already-parsed, caller-bounded conversation value. The thin
wrapper invokes the existing lossless provider parser and serializes its
`ConvModel`; it contains no new content-type, branch, author or attachment
mapping rules. OpenAI already supports an empty parser context without an asset
index. Anthropic's existing `Env&` parameter is unused; the patch adds an overload
without it, while keeping the original signature as a forwarding wrapper. The
existing import caller remains source-compatible and runs the same parser body.

The four patch paths are the new header and implementation, plus the small
Anthropic overload changes in `export_internal.h` and `export_anthropic.cpp`.
No root build change is needed: existing source globbing includes the new C++
translation unit. No database/schema migration or new C ABI symbol is added.
Native consumers link against the existing static core. A Python C ABI bridge
would be an additional B decision; D does not pretend this patch is already
active in its Python reference path.

The result includes full `raw` input, existing conversation export metadata,
message raw metadata, authors, content, parent indices, statuses, version-group
indices, counts and preservation counters. These are domain mapper outputs;
an upstream adapter supplies immutable bytes, source hash and document selector.
The full importer also enriches wrapper/archive/source pointers and materializes
assets, which this one-element hook does not do.

## Explicit limits and transformation accounting

- Asset pointers/attachment metadata are retained, but `asset_resolution` is
  `not_evaluated`. There is no asset manifest, resolver, network request or file
  access. Available attachment materialization parity is **not** claimed.
- Database IDs are never generated. Parent and version groups use the existing
  intermediate model's indices, including unresolved information in raw metadata.
- The existing parsers substitute the current clock when source timestamps are
  missing. The wrapper omits normalized `created`/`updated` fields to avoid
  presenting those import defaults as source history. Original source timestamps
  remain unchanged in raw input/message metadata. This representation change is
  recorded in the output, alongside source-selector and ID differences.
- The hook is DB-free and returns a deterministic view for the checked fixtures.
  It is not a claim of provider-schema coverage beyond the existing parser.
- Callers bound input before mapping. This optional hook creates no competing
  limits, transport, workflow, configuration engine or graph storage.

## Actual verification

`domain_mapper_test.py` extracted only affected existing files from both exact
Git pins, checked/applied the patch in temporary directories, then compiled the
new wrapper, patched Anthropic translation unit and public-API regression against
the already-built pinned-main `libloom_core.a`. GCC 13.3 / C++20 / WERROR passed.
There was **no full native rebuild** and no mutation of existing repository files.
B patch applicability was verified; a full B runtime rebuild is not claimed.

The resulting binary tested both providers with safe generated fixtures:
alternative branches, parent/group indices, authors, unknown conversation/message
fields, complete raw input, retained attachment metadata with no invented
availability, negative shape/index cases and repeat output stability. A separate
stdin/stdout probe returned JSON for another Anthropic fixture. Both ran in a new
empty directory that remained empty: no database or other output tree appeared.

Result: **native regression PASS; two providers; stdin/stdout probe PASS**.
Binary SHA-256:
`cf835eb08d6360abc6bc612c8bcd3fcb81bb19bd993416973a946925a803403c`.

Reproduce after the pinned-main static build described in `native.md`:

```bash
python docs/reports/resource-graph-2026-10-09/domain_mapper_test.py \
  --build-directory ../build-resource-graph \
  --output-directory ../domain-mapper-hook
```

The retained executable `domain_mapper_test` runs regressions without arguments;
with argument `openai` or `anthropic` it reads one conversation JSON from stdin
and emits its mapped JSON to stdout. That mode is an executable proof of the
optional seam, not a silently installed production dispatch path. The runner
also writes a reproducible receipt when an output directory is supplied.

Final D review replaced parser exception payloads with a fixed error code so
source content cannot leak through diagnostics. The targeted compiler, two-provider
regression and JSON probe were rerun successfully; final receipt and log are
`domain-mapper-receipt.json` and `domain-mapper-test.txt`.
