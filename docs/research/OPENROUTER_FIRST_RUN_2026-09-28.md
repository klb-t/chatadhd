# First authorized pilot: stopped before inference

**Continuation:** the setup requirement below has been superseded by
`OPENROUTER_BYOK_CORRECTION_2026-09-28.md` after the owner disabled reset and
authorized correcting our overbroad BYOK guard. The evidence here remains the
unaltered history of the zero-POST first attempt.

The owner confirmed the key setup and proposed USD 2 pilot on 2026-09-28.
Activation commit: `7ebd54556c001d12e95f0b366de6b003bc83730f`.
Actions run: https://github.com/klb-t/chatadhd/actions/runs/36444725653

The configured secret was available. Input/pricing preparation and the durable
reservation succeeded. The authenticated key-metadata check stopped execution
with `dedicated_nonresetting_inference_key_required` before any model POST.
That first implementation combined several possible causes; the error alone
does not establish which field was responsible or that the owner configured it
incorrectly. No gate is being relaxed to make the run pass.

The downloaded results artifact was checked against GitHub's published SHA-256:
`9134ff71f1d7f61221ce6970558c91cc11eca127b984937cea10b5ab833e394b`.
It is retained verbatim as `inputs/openrouter-first-attempt-2026-09-28.zip`.
Its ledger has **zero attempts**, no key-check success, and no response files.
Therefore zero model-inference requests were sent by this run. The score file's
32 `not_attempted` rows are missing observations, not model failures or accuracy.
Canonical execution-manifest hash:
`11b2687bece60ece817f2fded2c83e66263c700b50afd8895eba936e8a300483`.

A separate read-only credential preflight is being prepared to report just
normalized limit/reset/privilege/BYOK flags, without key labels, identifiers,
account metadata, credentials or raw API replies. It performs GET /key only.
The original reservation/history remains intact. Any subsequent execution must
explicitly record the verified zero-POST outcome; do not rerun the old Actions
job or pretend the first attempt completed extraction.

## Read-only diagnosis

Credential preflight run:
https://github.com/klb-t/chatadhd/actions/runs/36445312994

The authenticated GET succeeded and returned an ordinary, nonprivileged key.
Its limit and remaining balance are both USD 2. Two settings differ from the
prepared pilot guard: `limit_reset: weekly` and `include_byok_in_limit: false`.
The specific refusal is `key_reset_enabled`; the BYOK-inclusive limit check would
also refuse after that is corrected. The sanitized result is retained in
`inputs/openrouter-key-diagnostic-2026-09-28.json`; no key value or label is stored.
This is our experimental spending guard refusing those settings, not OpenRouter
rejecting a valid inference key. Ordinary inference credentials cannot edit their
own management settings, so the owner must change them in OpenRouter.

Required continuation: set reset to none and include BYOK usage in the limit for
the same dedicated key. The GitHub secret need not be replaced if its value stays
the same. After confirmation, perform the read-only check again. First execution
had zero POSTs and no uncertain attempt; a documented new experiment ID can then
run the same frozen development cases without concealing or overwriting that run.

No paid inference took place in either workflow. A newly authored, separately
frozen conversation-context corpus was prepared during this diagnosis; see
`CONVERSATION_CONTEXT_CASES_2026-09-28.md`. It still needs a scorer/live adapter.
