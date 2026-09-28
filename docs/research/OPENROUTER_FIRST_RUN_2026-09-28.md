# First authorized pilot: stopped before inference

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
