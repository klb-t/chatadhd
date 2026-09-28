# Bounded OpenRouter experiment runner

`openrouter_runner.py` is an opt-in research transport for real, text-only model
experiments. It writes experiment files, never Loom graph state. A successfully
received completion is **not** evidence that extraction, grounding, logic, or
cross-topic analogy is correct. Score saved completions independently.

The supplied unit tests use mocked HTTP and a fake credential. They make no
model calls and measure no model quality.

## Interface

```python
plan_manifest(manifest)  # no network, no credential reads
estimate_reservation(body)  # returns input_token_allowance, minimum_reservation_usd
run_manifest(manifest, run_dir)  # real requests; resume exact manifest only
inspect_key(manifest)  # GET /key only; sanitized diagnostics, no inference or files
```

```sh
python loom/tools/structure/openrouter_runner.py plan pilot-manifest.json
python loom/tools/structure/openrouter_runner.py inspect-key pilot-manifest.json
python loom/tools/structure/openrouter_runner.py run pilot-manifest.json --run-dir /private/pilot-run
python -m unittest discover -s loom/tools/structure -p 'test_openrouter_runner.py' -v
```

`run` is a spending operation. Review `plan` and the selected inputs before using
it. Keep credentials out of manifests, command-line arguments, repository files,
chat messages, and reports. Configure **one** of these in the execution environment:

- `OPENROUTER_API_KEY`, injected through a secure environment/secret mechanism.
- `OPENROUTER_API_KEY_FILE`, pointing to a private regular UTF-8 file outside the
  repository, readable by its owner only (`0600`). Its content is the API key.

The runner does not discover existing application secrets. An environment
variable assignment containing a literal key can enter shell history; use secure
injection or a private file written through an appropriate local credential UI.

Before the first paid POST of an invocation, `GET /api/v1/key` must establish a
finite **total** key limit no greater than the manifest budget, sufficient
remaining limit for every untouched reservation, no periodic limit reset,
`is_management_key: false`, and `include_byok_in_limit: true`. A management or
provisioning key is refused. Use a dedicated inference key for the pilot, without
concurrent unrelated requests. This is a deliberately stricter experiment guard,
not a general OpenRouter requirement. The runner never changes key settings.

`inspect-key` checks the full manifest reservation with one authenticated GET and
no POST. It creates no run directory or artifacts. Its JSON includes `gate_valid`,
a fixed `reason`, and sanitized key metadata; exit status is 0 when the guard
passes and 2 when it refuses the key or cannot inspect it. This command does not
check live endpoint availability or replace the pricing-freshness check in `run`.

Diagnostics distinguish `key_reset_enabled`, `key_reset_value_invalid`,
`key_management_flag_missing`, `key_management_flag_invalid`,
`management_key_forbidden`, `key_provisioning_flag_invalid`, and
`provisioning_key_forbidden`. Missing management metadata is never interpreted as
`false`. A missing optional provisioning flag preserves the existing accepted
behavior; if present, it must be explicitly `false`.

On preflight failure, `run` saves `ledger.key_check_failure` before raising the
fixed error. The metadata whitelist contains only normalized `limit` and
`limit_remaining`, `limit_reset` (`daily`, `weekly`, `monthly`, or null), and
boolean/null management, provisioning, and BYOK flags. Absent values become
`"missing"`; invalid values become `"invalid"`. Unknown strings, labels, key hashes,
creator identities, error bodies, headers, and credentials are never copied.
Transport failures retain only a fixed reason. Preflight failures create no paid
attempt record and do not change the existing resume policy; any later retry must
still follow the experiment's authorization and first-attempt accounting rules.

## Manifest contract

```json
{
  "schema": "loom.openrouter_manifest/1",
  "experiment_id": "structure-pilot-001",
  "budget_usd": "1.00",
  "max_requests": 1,
  "metadata": {"purpose": "example; replace model and pricing with live evidence"},
  "pricing_evidence": [{
    "model": "vendor/model-version",
    "provider": "provider/endpoint-tag",
    "pricing": {"prompt": "0.000001", "completion": "0.000002", "request": "0"},
    "source_url": "https://openrouter.ai/api/v1/models/vendor/model-version/endpoints",
    "retrieved_at": "2026-09-28T00:00:00Z"
  }],
  "requests": [{
    "id": "case-001-method-a",
    "metadata": {"case_id": "case-001", "method": "a"},
    "reservation_usd": "0.02",
    "body": {
      "model": "vendor/model-version",
      "messages": [{"role": "user", "content": "Extract a candidate graph from this text."}],
      "max_tokens": 1024,
      "temperature": 0,
      "stream": false,
      "response_format": {"type": "json_object"},
      "provider": {
        "only": ["provider/endpoint-tag"],
        "allow_fallbacks": false,
        "require_parameters": true,
        "max_price": {"prompt": 1, "completion": 2, "request": 0, "image": 0}
      }
    }
  }]
}
```

The example uses placeholders and stale example pricing; it is not runnable
without replacing them. Source pricing values are **USD per token**, whereas
`provider.max_price.prompt/completion` are **USD per million tokens**. Evidence
must refer to the selected provider endpoint, not the model's lowest advertised
price. Preserve its full pricing dictionary. The runner checks supplied evidence;
it does not authenticate the saved snapshot or prove a provider's billing rules.
Before paid calls, evidence must be at most 24 hours old (future clock tolerance
five minutes). Refresh and review evidence when preparing a new manifest. Do not
edit an already-started manifest to refresh it.

Only one explicit model and one provider tag are allowed per request. Model
routing variants (`:nitro`, etc.), automatic routers, fallback lists, tools,
plugins, search activation, image/audio content, cache-write directives, and
streaming are rejected. `response_format` may be `json_object` or a strict
`json_schema` object. Optional sampling fields are `temperature`, `top_p`, and
integer `seed`; provider parameter support is required. Optional `reasoning` must
be exactly `{"enabled": false}`. No automatic output repair is enabled.

Nonzero unknown charge categories fail planning. `input_cache_read` is allowed
only at or below prompt price. A catalog `web_search` rate is retained but cannot
be activated by the allowed request fields. `discount` is catalog metadata in
`[0,1]`. Nonzero per-request, image, cache-write, and separately priced internal
reasoning charges are rejected. This small initial surface can be extended in a
separately reviewed experiment when its costs are explicitly accounted for.

## Spending and replay rules

For each request, the minimum reservation is:

```
input allowance = UTF-8 byte length of the complete serialized request body
                  + 1024 + 32 * number of messages
USD allowance = input allowance * max prompt price per million / 1,000,000
                + max_tokens * max completion price per million / 1,000,000
```

This deliberately generous allowance is **not a tokenizer or billing guarantee**.
Provider framing, reasoning, pricing changes, BYOK fees, or accounting behavior
can differ. A dedicated server-side key cap is the strongest available guard.
There are at most 256 manifest requests; sum of all reservations must fit the
budget. Reservations remain consumed locally even after a cheap result, failure,
or uncertainty; spare credit does not automatically create extra requests.
A reported cost above its reservation stops the run and remains stopped on resume.
HTTP 401, 402, 403, and 429 also stop after retaining the first error response;
`stopped_reason` records `terminal_http_<status>`. Untouched requests stay unused,
and resume makes no further calls for a stopped run.
Missing usage/cost remains unknown, never zero. No automatic retries, provider
fallback, model fallback, or repeat until a passing response is implemented.

Requests run sequentially. A POSIX file lock prevents concurrent use of the same
run directory and releases on process death. Each attempted request is recorded
atomically and fsynced **before** its POST. Completed bytes and their SHA-256 are
stored before recording the terminal outcome. Manifest and request hashes bind
resume to the original inputs. An interrupted `started` record becomes
`uncertain`, even if the process may have stopped before actually sending it.
Such requests are never retried. Resume may process only untouched later requests.
Changing the manifest or editing/removing the ledger is not a supported retry
mechanism. A deliberate repeat requires a newly reviewed experiment and budget.

If the process dies after persisting a response but before updating the ledger,
that response remains on disk and the attempt stays uncertain. Inspect the saved
bytes without sending again. If initial metadata was written but no ledger
exists, the runner refuses to repurpose the directory. These conservative cases
trade lost attempts for avoiding duplicate charges.

## Artifacts and status

`run_dir` contains `manifest.json`, `plan.json`, `ledger.json`, `run.lock`, and
`<request-id>.response.bin`. Responses preserve the exact received bytes (including
HTTP error bodies); no generated content is promoted into the knowledge graph.
Credential-containing responses are not saved. The API key and key metadata
labels/account identifiers never appear in normal artifacts or error messages.
The run directory may contain source texts and generated content: keep it private
when using private inputs, and publish only deliberately reviewed artifacts.

`ledger.attempts` is an ordered prefix of the manifest requests. Every attempt
contains `id`, `request_hash`, `reservation_usd`, `state`, and timestamps. Saved
responses add `response_file`, `response_sha256`, and `http_status`.

| State | Meaning |
|---|---|
| `started` | Durable write-ahead record; process might still be making the call. |
| `completed` | One text choice, `finish_reason: stop`, no refusal/tool calls; semantics unchecked. |
| `rejected` | Response retained, but malformed, error envelope, refusal, or non-stop finish. |
| `http_error` | Non-200 bytes retained; no retry or automatic assumption of zero cost. |
| `uncertain` | Interrupted, timed out, oversized, or otherwise indeterminate; no retry. |

The CLI prints the ledger, not response contents. A completed CLI invocation is
not necessarily a successful model experiment: inspect every state, the number of
untouched requests, and `stopped_reason`. `reported_cost_usd` exists only when a
valid usage cost was reported. Failures and abstentions belong in the denominator
of evaluation, alongside structural correctness and source-grounding scores.

Transport sends credentials only to the fixed HTTPS OpenRouter origin and
refuses redirects. Both socket inactivity and the whole request have a 60-second
limit; the latter also stops a continuously trickling response. Responses are
capped at 2 MiB, request bodies at 256 KiB, and manifests at 16 MiB. Whole-request
deadlines and file locking currently require a POSIX main-thread process. There
is no subprocess daemon or post-exit background inference.

## Documentation checked

Official OpenRouter references, checked 2026-09-28:

- [Provider selection and max price](https://openrouter.ai/docs/guides/routing/provider-selection)
- [Current API key metadata](https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key)
- [Structured outputs](https://openrouter.ai/docs/guides/features/structured-outputs)

Provider capabilities, availability, prices, and key metadata can change. An
unavailable endpoint or unsupported output format is a recorded failure, not an
instruction to silently change the experiment.
