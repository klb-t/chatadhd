# Separate-key staged executor

`research_programme_runner.py` succeeds the prepared EUR 5 admission gate. It
does not change the historical USD 2 runners or their ledgers. The owner has
already authorized the separate programme; the runner does not invent another
budget-consent step. The exact provider USD limit, dated EUR/USD conversion and
current quotes still have to pass admission before spending.

The public [runner preset](billing/runner-policy.json) intentionally has a null
USD cap. Create an operator-owned private copy with a USD cap no greater than
both the provider key limit and the dated EUR authority. Routes, timeout,
provider/model aliases, component accounting, unit validation and ×10 guard
settings are editable data. The transport fixes the credential-bearing origin
to `https://openrouter.ai`; Jev uses its `/api/alpha/decisions` route there.
The public preset disables environment proxies; the private operator copy may
set `transport.proxy_mode` to `environment` when the runtime requires its egress
proxy. Origin pinning and redirect rejection remain enabled in either mode.
`transport.backend` defaults to `urllib`. Selecting `requests_session` reuses
one pooled connection session and requires explicit `pool_connections`,
`pool_maxsize`, and `pool_block` settings. It preserves body bytes, does not
retry or follow redirects, and requires the raw `read1` capability before
making requests. Missing dependencies/capabilities fail before dispatch.
Connection reuse can reduce repeated setup latency; its actual benefit must be
measured in the execution environment. The backend selection changes transport
only, while the immutable request manifest and cumulative ledger remain intact.
Optional `transport.headers_by_method` supplies caller-owned GET cache headers.
Credential, host, content framing and encoding headers remain protected; header
values cannot contain newlines. A successful no-cache read alone does not
establish whether earlier delayed metadata came from caching or indexing.

The [request manifest contract](../RESEARCH_PROGRAMME_MANIFEST.md) accepts exact
body files for every prepared stage. Its adaptor projects the existing 432
requests into the old transport's exact canonical wire bytes, without changing
prompts or invoking a compiler/scorer. It also accepts prepared synthetic DEV,
frontier/reply and later repeat/unseen bodies. An abstract plan without an exact
prepared body is not executable. All manifest and request hashes are validated
from one frozen input snapshot before dispatch, and consumed bytes are copied
into the private attempt record.

The private evidence object contains `fx`: `base_currency`, `quote_currency`,
`usd_per_eur`, `source_ref`, `checked_at`, `rate_date`, `raw_file`, and
`raw_sha256`. The runner verifies the declared rate against the exact dated ECB
XML snapshot, including its hash and configured age. Offline `plan` may also
consume normalized `pricing` rows from the existing admission-gate contract.
`run` obtains current pair-specific endpoint quotes itself before the stage.
The conversion basis is a reference rate, not proof of card settlement costs.

```sh
python3 -B loom/tools/structure/research_programme_manifest.py adapt-analysis \
  docs/research/analysis_optimization_2026-10-02/prepared \
  --programme-id thread7-new-key-2026-10-04-eur5 \
  --stage-id stage1-cheap-jev-432 --output /private/stage1-requests
python3 -B loom/tools/structure/research_programme_runner.py plan \
  --policy /private/operator-policy.json \
  --manifest /private/stage1-requests/manifest.json \
  --evidence /private/public-evidence.json
python3 -B loom/tools/structure/research_programme_runner.py run \
  --policy /private/operator-policy.json \
  --manifest /private/stage1-requests/manifest.json \
  --evidence /private/public-evidence.json \
  --private-dir /private/programme-records \
  --key-file /private/credential.key
```

Reuse the same private directory across every stage. It must be outside every
Git tree and owner-only. The key is read from an owner-only regular file,
without symlinks or hard links. One terminal text-file newline is removed; the
fingerprint hashes exactly the credential bytes sent in the Authorization
header. Fresh key metadata is obtained with that same credential. By default,
provider usage must equal cumulative verified receipts. Credentials, account labels and raw
model responses are never included in aggregate stdout or public reports.

Before each POST, a cumulative SQLite reservation commits with synchronous
FULL durability, then immutable started/request records are fsynced. The first
raw response is saved before parsing, including errors or partial reads. Exact
generation identity, selected model/provider aliases, API type, reported cost
and explicit credit-versus-BYOK evidence must agree. Actual cost, token counts
and latency remain private; aggregate stage/cumulative cost receipts are safe
to report. Completed receipts are reconstructed from their original raw
response and generation bytes when the ledger reopens.

Any ambiguous, unknown-cost or mismatched attempt stops continuation. A durable
stop record survives another invocation. Orphan evidence blocks with and
without a ledger; it is never adopted into a replacement ledger, and a request
is never retried automatically. Existing completed operations are not sent
again; the returned saved receipt explicitly says no fresh preflight occurred.
There is no automatic adoption or unconditional stop-reset command. Before a stop, bounded
read-only checks can collect delayed generation metadata or lagging lower key
usage. Their count/delay/pending HTTP statuses are policy data, every returned
raw metadata record is retained, and no POST is repeated. Higher/unknown usage,
identity/cost contradictions or exhausted reads stop immediately. Stage IDs are
durably bound to one manifest hash before provider access, and a generation ID
cannot certify multiple attempts.

The optional `provider_usage_lag.mode: "allow_verified_lower_usage"` requires an
explicit `authority_ref`. It admits a lower observed usage reading only when
every cumulative attempt is completed with replayed unique generation identity,
matching actual cost and explicit credit billing. All other strict gate checks
must pass. The raw observed usage is preserved, and the strict equality check
remains false in the readiness witness. Admission separately uses the minimum
of provider-reported remaining credit and the configured cap minus verified
actual costs and unresolved reservations. Higher usage, unknown cost, identity
contradictions or incomplete attempts continue to stop spending. The public
preset and original standalone admission gate retain strict equality.

After a clean stop involving only completed verified attempts, `reconcile` uses
GET requests, exact-key binding, fresh caps/FX/quotes and full receipt replay. A
successful resolution appends an immutable record containing the original STOP
projection and hashes of its unchanged receipts, then clears only the active
STOP projection. It creates, adopts and retries no paid attempt. The same
immutable manifest can then resume, skipping its already completed operations.
Every reopening validates the immutable resolution file against its SQLite
record and rechecks the original STOP witness hashes. Missing, changed or
orphaned resolution evidence blocks before provider access.

```sh
python3 -B loom/tools/structure/research_programme_runner.py reconcile \
  --policy /private/operator-policy.json \
  --manifest /private/stage1-requests/manifest.json \
  --evidence /private/public-evidence.json \
  --private-dir /private/programme-records \
  --key-file /private/credential.key
```

The separate `reconcile-captured` command can resolve an uncertain attempt only
when its original request, complete HTTP 200 first response, generation ID and
configured pending generation-read statuses are already durably captured. It
fetches its own GET-only proof; it accepts no replacement response or imported
generation record. Exact generation identity, model/provider/API type, cost,
credit billing and the original reservation must agree. Reserved attempts,
partial responses and other uncertainty remain blocked before external access.

```sh
python3 -B loom/tools/structure/research_programme_runner.py reconcile-captured \
  --policy /private/operator-policy.json \
  --manifest /private/stage1-requests/manifest.json \
  --evidence /private/public-evidence.json \
  --private-dir /private/programme-records \
  --key-file /private/credential.key
```

Late proof appends an immutable `attempt-resolution.json` record and private
generation-read bytes. The original uncertain SQLite attempt, result receipt
and all pending-read bytes remain unchanged. Reopening rebuilds a verified
completed projection from those bound originals and the late proof. Fresh
key/cap/FX/quote checks must still clear the active STOP before continuation; a
valid late proof with a failed budget check retains the STOP. Resume uses the
completed projection to skip the original operation without another POST.

Cooperative pause uses `pause.request_filename`, configured signals or a
caller-selected operation limit. A request received during POST finishes the
first response, generation proof and key/budget reconciliation before returning
`paused`. It creates no durable STOP. Clear the owner-only request file or
invocation limit before resuming the same manifest and ledger. Signal handlers
are restored when the invocation ends. This applies to processes started with
pause support; editing source cannot retrofit a pause handler into an older
running process.

```sh
python3 -B loom/tools/structure/research_programme_runner.py request-pause \
  --policy /private/operator-policy.json --private-dir /private/programme-records
```

The ×10 guard compares like-for-like rolling forecasts by default, rather than
comparing a conservative reservation floor with an underused actual response.
Its data selects per-operation or stage-total scope, baseline metric/window,
factor and manifest-bound confirmation evidence. An absent/zero baseline is
explicitly labelled unknown; the preset uses the already authorized initial
programme scope. The preset applies escalation admission once to the full
frozen stage; later rolling samples cannot expand that same admitted plan.
An editable `each_operation` evaluation boundary is also supported. Fresh key,
price-age and cumulative-budget checks still run after each operation. The
ordinary EUR/USD cumulative cap still applies.

Declared unit bounds and adapter byte allowances are forecasts, not a provider
billing guarantee. The preset rejects prompt quantities below exact request
bytes and chat output quantities below the explicit token maximum. Every quote
component must have a bound; an unknown component stops preflight. A reported
cost exceeding its reservation stops further spending while retaining the
actual receipt. Scoring and stage selection remain with the existing research
tools.

Verification: 103 focused offline regressions plus the existing 8 gate checks
pass. These use fabricated keys, rates, prices, receipts and transport; they
cover all 432 frozen requests, prepared later-stage formats, crashes, orphan
files, billing replay corruption, pricing/FX, exact key identity, BYOK, guard
baselines, durable stops, pooling, cooperative pause/resume, GET-only STOP
resolution and late-generation proof replay. Independent reviews also exercised
21 fabricated late-proof cases, including rejection, budget and evidence
tampering paths. No real credential was read or provider call made
by this implementation lane.
