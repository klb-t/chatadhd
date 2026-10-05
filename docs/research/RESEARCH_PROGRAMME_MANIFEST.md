# Exact request manifests for research programmes

`loom/tools/structure/research_programme_manifest.py` provides an offline input
contract for the programme runner. It locates prepared requests and verifies
their bytes; it does not compile prompts, reconstruct candidate graphs, score
answers, access credentials or call a provider.

Each `loom.research_programme_manifest/1` has explicit `programme_id`, `stage_id`
and an ordered `operations` array. A stage's operation IDs must be unique.

```json
{
  "schema": "loom.research_programme_manifest/1",
  "programme_id": "model-research",
  "stage_id": "paired-source-commitment",
  "operations": [
    {
      "operation_id": "arm-a.case-1",
      "route_id": "chat",
      "request_file": "requests/EXACT_SHA256.json",
      "request_sha256": "EXACT_LOWERCASE_SHA256",
      "model_id": "openai/gpt-4.1-mini",
      "provider_id": "openai",
      "units_upper_bounds": {
        "prompt": 8192,
        "completion": 192,
        "request": 1,
        "input_cache_read": 0,
        "web_search": 0
      }
    }
  ]
}
```

The placeholder hash strings above illustrate the shape; a real manifest uses
the exact 64-character SHA-256 of the referenced file. Files are relative to
the manifest directory and cannot resolve outside it. The loader checks the
entire file, including whitespace, before checking that request `model` equals
`model_id`, that `provider.only` equals `[provider_id]`, and that fallbacks are
disabled. The request shape must match the selected `chat` or `jev` route.
Transport receives the original bytes rather than a reserialized object.

`units_upper_bounds` contains quantities in the exact component names used by
the price quote. Values must be finite and nonnegative. Every priced component
must be covered by the separate budget gate. A component absent from this
mapping is unspecified; it is never an implicit zero. An optional
`minimum_reservation_usd` retains an older prepared reservation as a separate
floor, without treating historical prices as a fresh quote.

The Python entry points are:

| Entry point | Result |
|---|---|
| `load_manifest(path)` | Validated manifest after every body hash and identity check |
| `load_manifest(mapping, base_dir=directory)` | The same check with an explicit directory |
| `read_manifest_bytes(raw)` | Strict parsing of one snapshot, whose same bytes the runner hashes |
| `read_request(operation, directory)` | Exact verified request bytes |
| `load_operations(path)` | Operation mappings plus `request_bytes` and `request_body` |
| `adapt_prepared_manifest(source, output, programme_id=..., stage_id=...)` | One projected manifest and content-addressed body files |
| `adapt_prepared_manifests(sources, output, programme_id=..., stage_id=...)` | A projected manifest preserving source order |
| `adapt_analysis_optimization(prepared, output, programme_id=..., stage_id=...)` | Projection in the arm order recorded in `plan.json` |

The adaptors support the existing OpenRouter and Jev embedded-body manifests,
the 60 nested stage-2 requests, the 72 extraction-preview rows, and historical
frontier/followup rows containing prepared bodies. Abstract frontier
preparations without concrete bodies remain incomplete inputs. Projection
does not establish source-code freeze validity, current endpoint capability,
account identity, native execution, or semantic quality.

Embedded bodies use the existing transport's canonical UTF-8 serialization
with no trailing newline. Body hashes defined by each preparation format are
checked where present. Whole-row `request_sha256` in frontier preparations is
a different hash scope and is not confused with the body-byte hash. Exact
source-file hashes and original request/arm IDs remain in metadata. Existing
artifacts are reusable only when their bytes match; a changed projection must
use a new output directory.

The owned [request-units.json](model_research_2026-10-04/billing/request-units.json)
preset supplies configurable framing,
message, request and Jev completion allowances. The adaptor records the exact
canonical policy data and `units_policy_sha256` in manifest metadata; the
loader checks that binding. A caller's explicit `units_policy` replaces the
preset. Default quantities retain the existing byte-based allowance: request UTF-8
bytes plus 1024 framing units and 32 units per chat message; chat completion
uses `max_tokens`, while the existing Jev preparation declares zero completion
units. One request unit is included. Historical frontier prompt/completion
allowances, including their configured completion multiplier, take precedence.
These allowances are declared with `billing_bound_guaranteed: false`; they are
surrogates, not tokenization or billing guarantees.

Callers can explicitly supply `units_policy`, `units_upper_bounds`,
`units_by_arm`, and `units_by_operation`. Unit overrides merge in that order
after the source allowance.
Unknown arm or operation selectors fail before writes. Optional quote
components such as cache reads and web search require explicit quantities when
priced, including explicit zero when appropriate.

The CLI can project existing bodies and verify the result without a network:

```bash
python -m loom.tools.structure.research_programme_manifest adapt-analysis \
  docs/research/analysis_optimization_2026-10-02/prepared \
  --output /private/programme-inputs/paired \
  --programme-id model-research --stage-id paired-source-commitment
python -m loom.tools.structure.research_programme_manifest verify \
  /private/programme-inputs/paired/manifest.json
```

`adapt` accepts one or more prepared files. `--units-config` accepts a JSON
object with the policy and override fields above. This module does not launch the
prepared operations. Its tests check 432 paired bodies, the other prepared
stages, exact whitespace/Unicode preservation, duplicate IDs, route/identity
mismatches, invalid units, body tampering, traversal, and immutable reuse.
