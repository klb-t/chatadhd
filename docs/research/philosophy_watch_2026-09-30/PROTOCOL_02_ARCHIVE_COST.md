# Offline archive-cost boundary audit: protocol 02

2026-09-30. No provider call, credential read, owner archive read or production
edit. Inspect the existing `loom/tools/eval/archive_cost.py` using pure numeric
inputs and tiny disposable SQLite fixtures. Saved first results classify domain
validation and planning assumptions, not provider invoices or usage guarantees.

Numeric counterexamples: negative/NaN/infinite output ratio, negative prefix,
negative/NaN price, boolean counts and invalid fraction. Meaningful cost/count
domains are finite and nonnegative; this is not an artificial maximum budget,
model, scope, reasoning or acceptance restriction. Large finite values remain
permitted by the application design. Zero output/prefix should be valid.

Data counterexamples: a message with NULL status, filenames containing `?` and
`#`, and an excluded-only conversation. Compare counted messages/chars to an
independent query and source-file hash. A read-only estimator must not create an
unexpected sibling SQLite file while opening a URI-sensitive path.

Planning counterexamples: cache-read price applied to the first/only reading,
uniform batch discount on an endpoint that has no batch capability, fractional
conversation counts. These are assumptions requiring labels/capability checks,
not proof of actual billing. Preserve returned figures and denominator choices.

Freeze code/pricing/probe hashes before execution. Save all first responses and
exceptions in `ARCHIVE_COST_FIRST_RESULTS.json`; do not fix source code here or
rerun and substitute results. Send concrete reproducible defects to the owner of
the method; any correction receives a new audit snapshot.
