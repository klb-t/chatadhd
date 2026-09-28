# Real-model pilot through GitHub Actions

Status on 2026-09-28: execution path prepared; no paid model requests have been
made by this workflow. The checked-in request is disabled. This path permits an
explicit, bounded experiment when the assistant's own execution environment
cannot reach OpenRouter. It does not relay the assistant's network traffic.

## What the owner needs to configure

1. Create a dedicated key at [OpenRouter API keys](https://openrouter.ai/settings/keys).
   Set a total spending limit of **at most USD 2**, with **no periodic reset**.
   Include BYOK usage in that limit. The runner checks the current-key API fields
   `limit`, `limit_reset` and `include_byok_in_limit` before sending inference
   requests; an unlimited or resetting key is rejected. The normal chat key is
   unnecessary for this pilot.
2. Open [this repository's Actions secrets](https://github.com/klb-t/chatadhd/settings/secrets/actions).
   Choose **New repository secret**. Name it `LOOM_OPENROUTER_PILOT_KEY` and put
   the dedicated key in the secret value. Do not put its value in chat, a commit,
   an issue, the request JSON or a workflow log.
3. Tell the assistant the secret is configured and confirm the pilot's USD 2
   ceiling, or state a lower ceiling. Adding the secret alone starts nothing.
   The assistant can then commit the concrete enabled request on
   `codex/loom-handoff-2026-09-28` and inspect the resulting run.

The key stays in GitHub's secret store. Only the presence-check step and the
inference runner receive it as an environment variable. The assistant reads
nonsecret logs and artifacts rather than retrieving the key. As with all
repository secrets, people able to change trusted workflow code must be trusted
to use it appropriately.

GitHub-hosted runner minutes and artifact storage may also count toward the
repository's GitHub Actions allowance or bill. This is separate from OpenRouter
spend. The workflow uses one Ubuntu job with a 30-minute job limit; the inference
command has a 20-minute limit, leaving time to retain a partial result.

## Concrete request and methods

The activation file is `docs/research/openrouter-pilot-request.json`. Its schema
is `loom.live_pilot_request/1`. It specifies `enabled`, `experiment_id`, `split`,
`methods`, `models`, `budget_usd`, `max_tokens` and `max_requests`. The default
request remains `enabled: false` until the owner has configured the key and the
bounded run is authorized.

The first experiment uses the independently authored development cases and the
native extraction prompt (`native_v1`). The alternative `native_anchors_v1`
adds a neutral UTF-8 token-offset table and grounding instructions to the same
prompt. It is a separate experiment, not a silent response repair. Gold labels
are excluded from inference prompts. Neither method turns model output directly
into accepted graph facts.

Initial model/provider candidates are:

| Model ID | Explicit provider |
| --- | --- |
| `qwen/qwen3-30b-a3b-instruct-2507` | `nebius/fp8` |
| `openai/gpt-4.1-mini` | `openai` |

These are request candidates, not a claim about the best available models. The
preparation step retrieves current public endpoint metadata and prices before
freezing the manifest. An unavailable or unsupported endpoint stops preparation;
it must not quietly choose another model or provider. The runner's own manifest
validation and cost accounting remain authoritative for request eligibility.

The pilot sends authored PL/EN evaluation text and the extraction contract. It
does not upload the owner's private conversation archives, attachments, source
repository or Gemini reports to the models.

## Execution and durable evidence

`.github/workflows/loom-openrouter-pilot.yml` listens only for a push changing
the activation file on the named branch in `klb-t/chatadhd`. Ordinary code edits
do not schedule a paid run. There is no pull-request trigger, `pull_request_target`
trigger, recurring schedule or manual-dispatch dependency. This also avoids
requiring this research workflow to be present on the default branch.

With a disabled request or absent secret, it logs a skip and performs no
OpenRouter requests. An enabled request proceeds in this order:

1. Validate the activation and its budget; require a new experiment identity.
2. Freeze authored inputs, source identities and current public model pricing.
   Run the transport's offline `plan` validation.
3. Check for an earlier reservation and upload a reservation artifact before
   any paid inference request. Failure to preserve the reservation stops the run.
4. Run the bounded transport with its write-ahead request ledger. First
   responses, failures and uncertain attempts are retained; the workflow adds no
   automatic retries.
5. Score retained responses, including a partial run when the ledger exists.
   Save the manifest, inputs, reservation, ledger, responses and scores together
   in a GitHub Actions artifact, even when a preceding step fails.

Artifact names are:

- `loom-openrouter-reservation-<experiment_id>`
- `loom-openrouter-results-<experiment_id>-<github_run_id>`

The reservation records the commit, workflow run ID, manifest hash and the fact
that it preceded paid requests. Artifacts request 90 days of retention, subject
to repository policy. They are interim evidence: copy the resulting nonsecret
report and ledger into the research branch after review, before retention
expires. The workflow has `contents: read` and `actions: read`; it cannot commit
or overwrite repository results itself.

The concrete commands are:

```bash
python3 loom/tools/structure/live_pilot.py prepare \
  --request docs/research/openrouter-pilot-request.json \
  --output-dir "$PILOT_DIR"
python3 loom/tools/structure/openrouter_runner.py plan "$PILOT_DIR/manifest.json"
python3 loom/tools/structure/openrouter_runner.py run \
  "$PILOT_DIR/manifest.json" --run-dir "$PILOT_DIR/run"
python3 loom/tools/structure/live_pilot.py score \
  --run-dir "$PILOT_DIR/run" --manifest "$PILOT_DIR/manifest.json" \
  --output "$PILOT_DIR/score.json"
```

`PILOT_DIR` is a new working directory selected by the caller; Actions assigns
one under its temporary runner directory. These commands document the actual
workflow interface, not a claim that live execution has already succeeded.

## Interrupted and repeated runs

Never click **Re-run jobs** to retry a paid attempt: attempts above one are
rejected. All pilot runs share one concurrency group with
`cancel-in-progress: false`. A new activation must be a single commit changing
the request itself. The complete request-file history is inspected before
inference: if any earlier revision already enabled the same `experiment_id`,
the workflow refuses another spend, even if the old artifact has expired.
An existing reservation artifact independently blocks that identity.

This conservative rule also consumes an identity if an earlier enabled request
failed before inference or skipped because the secret was absent. A history
containing over 200 request-changing revisions stops for an explicit audit.
The check assumes the research branch's history is preserved: do not rewrite or
delete activation history to bypass it.

After an interruption, download the first result artifact and inspect the ledger
and OpenRouter's usage. A request whose delivery or billing is uncertain remains
reserved/spent for budgeting purposes. Do not infer that an empty response or a
timeout was free. A new experiment ID represents a deliberate new experiment
with a reconciled budget; do not automatically generate one to evade the guard.

Normal command failure and the inference command's timeout leave time for
`always()` artifact steps. A destroyed runner, platform outage or whole-job
cancellation can still prevent a final upload. The earlier reservation survives
if it was uploaded, but it is not a per-request remote ledger. The dedicated
nonresetting key limit is a further server-side budget control, not proof that
all responses or usage data were recovered. There is no claim of exactly-once
provider execution.

## Verification and primary references

The workflow uses SHA-pinned official actions, verified against their upstream
repositories: `actions/checkout` v7.0.1 at
`3d3c42e5aac5ba805825da76410c181273ba90b1`, and `actions/upload-artifact` v7.0.1 at
`043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`. Both exact-commit `action.yml` files
were checked: they use Node 24 and accept the configured inputs. The artifact
API documentation's current example specifies `X-GitHub-Api-Version: 2026-03-10`,
which is the header used here.

Local verification: YAML parsed; every shell block passed `bash -n`; all inline
Python blocks parsed; 12 isolated activation-gate scenarios and three mocked
reservation scenarios passed. The eight independent integration tests in
`loom/tools/structure/test_live_pilot_independent.py` passed without opening
repository gold or calling a network endpoint. They check annotation exclusion, capacity before
network access, model/provider identity and ambiguity, non-token charges,
explicit routing, immutable preparation, declared response aliases and rejection
of missing or foreign response identity. A hosted workflow run and paid
inference remain unverified until an enabled, authorized request completes.

- [GitHub: repository secrets and environment-variable use](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets)
- [GitHub: artifact listing, exact name filter and read permission](https://docs.github.com/en/rest/actions/artifacts)
- [GitHub: workflow triggers](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)
- [GitHub: Actions billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
- [OpenRouter: current-key information](https://openrouter.ai/docs/api/api-reference/api-keys/get-current-key)
- [Official checkout tag](https://api.github.com/repos/actions/checkout/git/ref/tags/v7.0.1)
- [Exact checkout action contract](https://github.com/actions/checkout/blob/3d3c42e5aac5ba805825da76410c181273ba90b1/action.yml)
- [Official upload-artifact release](https://github.com/actions/upload-artifact/releases/tag/v7.0.1)
- [Exact artifact action contract](https://github.com/actions/upload-artifact/blob/043fb46d1a93c77aae656e7c1c64a875d1fc6a0a/action.yml)
