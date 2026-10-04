# Analysis prompt data and native request preparation

These JSON-content `.prompt` files define reusable analysis contracts. Defaults
are compiled into Loom, so installed programs do not depend on the working
directory or on an unpacked repository. Owner overlays remain local data.
The registry prepares requests and reports validation; it never sends HTTP.

| Contract ID | Current use | Default validation |
| --- | --- | --- |
| `semantic.relation` | Knowledge extraction, `relation_v1` | `strict` |
| `semantic.occurrence_graph` | Knowledge extraction, `occurrence_graph_v1` | `strict` |
| `semantic.analysis` | Legacy analysis data prepared for the W11 adapter | `off` |

`semantic.analysis` preserves the old user-message prefix, UTF-8 codepoint
clipping preset, request parameters and body field order. The legacy callers
in `semantic_llm.cpp`, `batch_api.cpp` and `semantic_worker.cpp` still need their
own adapter. Merely registering this contract does not change those callers.

## Current C++ interfaces

The registry interface is source-private, in
[`prompt_contract.h`](../../src/extract/prompt_contract.h), namespace
`loom::extract::prompts`. This change adds no exported C ABI or HTTP route.

```cpp
Result<Contract> resolve(std::string_view id,
                        const std::filesystem::path& overlay_dir = {},
                        const Json& patch = Json::object());
Result<Contract> from_snapshot(const Json& definition);
Json overlay(const Json& base, const Json& patch);
Result<Json> prepare_request(const Contract& contract, std::string_view model,
                            std::string_view provider, const Json& bindings,
                            const Json& request_patch = Json::object());
Json validate_output(const Contract& contract, const Json& output);
std::vector<std::string> builtin_ids();
Json builtin_catalog();
```

`Contract` contains the effective `definition` and its canonical SHA-256
`hash`. `builtin_catalog()` lists compiled IDs, versions, file names,
`contract_hash` and `source_hash`; the latter hashes the exact source file
bytes. Catalog entries describe builtins, not the later effective owner patch.

For example, preparation without transport calls is:

```cpp
auto contract = prompts::resolve("semantic.relation", owner_prompt_directory,
                                owner_patch);
if (!contract) return contract.error();
auto request = prompts::prepare_request(*contract, model, provider,
    Json{{"input_json", source_input}}, Json{{"temperature", 0.2}});
```

Objects overlay recursively; arrays and scalar values replace entirely.
`resolve` reads only `.prompt` files in the explicit overlay directory, matched
by their object `id`. Missing files retain their compiled default; new IDs may
provide a complete contract. Duplicate IDs, malformed files, unsupported
execution fields and attempts to replace the selected ID return errors.
There is no lookup relative to the current working directory.

Messages contain literal strings and named binding objects. String bindings
retain their text; other JSON values render canonically. `input_json` always
renders as canonical JSON. A binding's optional `max_codepoints` is a
configurable UTF-8 codepoint clipping preset; `null` removes that clipping.
`request_parameters` overlays the generated provider body, then
`request_patch` overlays the final body. Explicit supplied messages do not
require unused template bindings. `transport.body_field_order` controls wire
serialization order without removing unlisted fields.
The caller model is the initial body value; an explicit
`request_parameters.model` replaces it, then `request_patch.model` wins last.
Provider body patches do not change transport settings: use a contract or
`prompt_patch.transport` for method, URL, headers, timeout or stream behavior.
An explicit `transport.url` wins over the provider base and `transport.path`;
otherwise trailing slashes are removed from the provider base before appending
the path (preset `/chat/completions`).

The prepared result includes `method`, `url`, `headers`, `body_json`, exact
`body_bytes`, `transport`, `contract_hash`, `output_schema_hash` and
`request_hash`. `request_hash` includes the prepared method, URL, headers,
body bytes, transport, schema hash and validation mode. The registry does not
inject authorization. Explicit owner headers remain explicit owner input;
the semantic transport adds the runtime API key only when Authorization is
absent. Do not store credentials in public prompt files or public previews.
Canonical JSON is used for identities and the `input_json` binding. The actual
provider `body_bytes` retains insertion order or explicit `body_field_order`,
preserving historical wire bytes; it is not replaced by the canonical hash
representation.

## Knowledge extraction configuration and preview

The existing native knowledge semantic stage uses these contracts through
`extract::semantic_fingerprint` and `extract::propose_semantics`, declared in
[`knowledge_semantic.h`](../../include/loom/knowledge_semantic.h). Its extract
stage parameters are `params.semantic` (or
`stage_params.extract.semantic` in a knowledge-run request).

| Setting | Meaning |
| --- | --- |
| Runtime `analysis_prompt_dir` | Explicit owner overlay directory; preset is `<data root>/prompts`. |
| Runtime `analysis_prompt_overrides` | Object mapping contract IDs to durable recursive patches. |
| Stage `prompt_id` | Selected contract; preset follows the native representation. |
| Stage `prompt_patch` | Recursive patch after the directory and durable configuration. |
| Stage `prompt_snapshot` | Explicit effective definition; ignores durable overlays for this selection. |
| Stage `validation_mode` | Final stage schema mode: `strict`, `lenient` or `off`. |
| Stage `request_patch` | Final provider-body patch for this stage. |
| Stage `preview` | Prepare selected chunks and report requests without HTTP or knowledge/cache writes. |
| Stage `call_overrides` | Object keyed by the preview's exact `chunk` ID; each value accepts `prompt_patch`, `request_patch`, `validation_mode`. |
| Stage `usage_estimate` | Explicit patch of the W2 usage estimate; cannot replace the execution operation ID. |
| Stage `require_usage_policy` | Require the shared W2 usage capability before dispatch. |
| Runtime `loom_semantic_usage_policy_required` | Durable version of the same requirement; either requirement blocks an unavailable capability. |

Existing semantic limits remain stage settings: `max_requests`,
`max_observations`, `max_chunk_bytes`, `max_input_bytes`, `max_output_tokens`,
`max_proposals`, `max_response_bytes`, `timeout_ms`, and `representation`.
Defaults and editable upper bounds are in `analysis_parameters.limits` and
`analysis_parameters.limit_bounds`. Effective `max_output_tokens` also follows
`request_parameters.max_tokens`, and `timeout_ms` follows
`transport.timeout_ms`; those wire settings take precedence over their
`analysis_parameters.limits` copies. Explicit stage `max_output_tokens` and
`timeout_ms` then update the call's contract before per-call/request patches.
To exceed a preset bound, raise it or set
that bound to `null`; for example:

```json
{
  "semantic": {
    "preview": true,
    "max_requests": 9,
    "prompt_patch": {
      "analysis_parameters": {
        "limit_bounds": {"max_requests": null}
      }
    }
  }
}
```

Positive representation counts still need positive integers; request and
total-input budgets may be zero. Native integer representation and correct
types are implementation requirements. `validation_limits` and the graph
parser's `json_nesting_limit` are also editable data; `null` removes the
corresponding preset cap. Native source IDs, exact source support, branch
separation and reversible candidate authority are independent of these
resource presets.

Preview reports `status: "preview"`, `requests: 0` and `request_previews` with
the prepared request fields plus `chunk`, source references, validation mode
and usage-policy decision. It requires no API key or configured model and
does not checkpoint a semantic run, create candidates or populate the cache.
This applies to semantic preparation through `propose_semantics`; a whole
knowledge run still performs its ordinary extraction and task/run persistence.
The semantic stage must use `llm: "auto"`; `llm: "off"` returns the disabled
stage status before request preparation.
With W2 installed, opening its shared capability may initialize its separate
ledger; preview does not make a usage reservation.

To inspect a change to one chunk, first preview, then use its exact ID:

```json
{
  "semantic": {
    "call_overrides": {
      "<chunk from request_previews>": {
        "request_patch": {"temperature": 0.2},
        "validation_mode": "lenient"
      }
    }
  }
}
```

This override affects that addressed prepared request; it does not mutate
runtime configuration or later calls. Selection and total stage budgets
remain stage-wide. Unused IDs are reported in `unused_call_overrides`.
Preview and execution share request preparation.

The run identity includes the effective `prompt_contract` snapshot, schema,
request patches, call overrides and relevant runtime configuration. Pin a
recipe by passing that definition as `prompt_snapshot`; explicit local patches
still apply. A snapshot alone does not pin provider configuration or source
input. Existing `_semantic_identity` checking rejects a changed full identity
on resumed work.

## Validation, provenance and recovery

| Mode | Schema report | Invalid output in the native semantic stage |
| --- | --- | --- |
| `strict` | Checked; violations reject (`valid: false`). | Rejected and uncached. |
| `lenient` | Checked; violations warn (`valid: true`, `schema_valid: false`). | Retained with explicit schema warnings and partial status; a schema-invalid response is uncached. Native-invalid candidates have no observed support. |
| `off` | Unchecked (`checked: false`, `schema_valid: null`). | Native checks still run. Native-valid responses may be cached under this selected mode; native-invalid output is retained without observed support and remains uncached. |

The schema validator never rewrites output or removes unknown fields.
`additionalProperties` produces a rejection or warning according to mode.
Descriptive metadata belongs under `extensions`; it does not enable execution.
Schema success is separate from native grounding, logical validity and truth.
An owner schema warning does not erase independently checked exact native
source support. Such lenient output preserves the native report and the schema
failure separately; it receives partial status and cannot populate the checked
response cache.
Native-invalid retained candidates report `unvalidated_model_output`, pending
review and `promoted: false`; changing the schema mode never grants observed
evidence or promotes a canonical claim.

The supported JSON Schema subset includes type, required, properties,
additionalProperties, items, enum, const, oneOf/anyOf/allOf/not, string length
and pattern, numeric bounds and multipleOf, array length and uniqueness, and
object property counts. Standard descriptive annotations are accepted.
There is no `$ref`, `format`, conditional schema or tuple-items execution.
Unsupported constraints are explicit strict errors or lenient warnings; off
bypasses schema checking. See `prompt_contract.cpp` for the complete supported
keyword list.

Candidate provenance stores contract ID/version/hash, schema hash, request
hash, response hash, actual wire model/provider, validation mode and usage
operation ID. Cache identity includes effective prompt/request/transport,
source input, representation and native decoding policy. Native-valid responses
without a checked-schema failure can populate the reusable cache; malformed,
failed and native-unvalidated responses do not. An `off` cache entry retains its
unchecked schema report and is keyed by that mode. Identical accepted requests
can reuse a cache entry across
runs, while changed effective contracts select distinct entries.

`captured_responses` retains the first available HTTP response bytes and hash,
status/error, chunk, request hash and operation ID before decoding or later
accounting. The configured response-byte cap bounds retention; a truncated or
failed response stays uncached. Checkpoints preserve this trace separately
from candidate acceptance. Cache-hit trace records have `from_cache: true`
and `body_kind: "model_content"`, since the cache contains model content rather
than the original HTTP envelope. Accounting failure is reported explicitly
and does not erase already captured model output.

`semantic_usage.h` optionally forwards to W2's `UsagePolicy` when that header
and implementation are integrated. Its options come from
`effective_usage_policy_options(rt.config())` and its ledger is
`<data root>/usage-policy.sqlite`. Cohorts group representation, endpoint and
model, rather than resetting the baseline for every prompt hash. Unknown
token or monetary use remains unknown. The default estimate has one request
and `input_bytes` measured as rendered message-content bytes
(`rendered_message_content_bytes`), not the full serialized HTTP body.
Input/output tokens and monetary cost are `null` without a forecast;
`max_tokens` is retained only as provenance `output_tokens_cap`, not treated
as expected use. Supply an explicit forecast through `usage_estimate` when
one is available. Provider-reported actual usage remains distinct from that
forecast. An unresolved request reservation is not a new execution grant.

Without W2 the capability explicitly reports `available: false`,
`guard_applied: false`, `authorized: false` and
`reason: "usage_policy_dependency_missing"`. The compatibility preset retains
the prior execution path; requiring the capability blocks HTTP. This fallback
implements no rolling guard. Integrate W2 first and test the combined build
before treating the shared usage guard as available. Usage admission does not
replace owner consent for paid calls.

## Jev recipe and W11 handoff

[`jev_active_refute_v2.recipe`](jev_active_refute_v2.recipe) records the exact
historical optional Decisions instrument, including q01/q02 questions,
directed supplied-relation and attributed-speaker inputs, temporal
interpretation, request/body hashes, output shape and measured provenance.
Its 45/48 result comes from historical diagnostic development judgments;
three unknown cases were classified as refuted. It establishes neither
independent production accuracy nor world truth. No new model calls were made
to adopt these data.

`.recipe` is intentionally not executable by this `.prompt` registry and is
not a new default. Decisions uses a typed `state/questions` body and `noul`
answers, not the native chat-completion proposal contract. A future caller
needs an explicit typed adapter, native source/attribution validation, usage
admission and owner approval before paid execution. Changing questions or the
historical `> 0.5` interpretation produces an unmeasured variant.

For W11, reuse `semantic.analysis` and this registry from the legacy analysis
callers, preserving exact input clipping/body bytes and their current parser
behavior first. Keep raw output and unknown fields, report schema validation
separately, and keep retry, cache, provider usage and first-response tracing
consistent with each caller's dispatch state. The current default `off` mode
preserves compatibility; it is not a claim that the legacy output was checked.

## Regeneration and offline verification

```sh
python3 loom/src/extract/gen_prompt_contracts.py
python3 loom/src/extract/gen_prompt_contracts.py --check
```

The generator compiles only `.prompt` files into
`loom/src/extract/prompt_contract_data.inc`; `.recipe` and this README are not
compiled. This registry is independent of the KB Pack generator.
It also refreshes the marked generated `#if 0` legacy export at the start of
`loom/src/extract/semantic.cpp`. W7's current `live_pilot.native_prompt()` reads
that exact `kGraphPrompt = R"PROMPT(...)PROMPT"` declaration with its existing
source regex. The export contains the builtin occurrence-graph system prompt
from data; it is excluded from compilation and does not select runtime or owner
overlay behavior. Edit `.prompt` data and regenerate rather than editing the
export. `--check` verifies both generated outputs without rewriting them.
The fixed legacy raw-string delimiter must remain representable until W7's
reader adopts the data/registry interface; the generator reports a delimiter
collision explicitly.

Offline fixtures live at
`loom/src/extract/tests/prompt_contract_probe.cpp.fixture` and
`loom/src/extract/tests/semantic_prompt_integration.cpp.fixture`.
The latter uses `ScriptedTransport` and a temporary database to check preview,
frozen default wire bytes, overlays/snapshots, per-chunk overrides, schema
modes, cache/provenance, lifted presets and missing-W2 dispatch blocking. They
make no live or paid requests. Compile them against the matching freshly built
`loom_core`, SQLite and miniz libraries; the W1 report records exact commands
and measured results.

```sh
bash loom/src/extract/tests/run_semantic_prompt_integration.sh /tmp/loom-semantic-prompt-integration
```
