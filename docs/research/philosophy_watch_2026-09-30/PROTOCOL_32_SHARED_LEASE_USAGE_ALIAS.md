# Independent follow-up: semantic mutation of a shared usage return

Before this lane's execution, round28 is known: nonfinite/non-JSON interpreter returns can prevent fee settlement. This new hypothesis is distinct: a billing extractor may return a mutable JSON object also referenced by a semantic interpreter closure. If the interpreter accidentally changes it after `usage_first.json` was saved, can settlement use the changed semantic-owned object? Freeze and test the preserved unchanged initial adapter SHA8a51e007 from round28, irrespective of later producer fixes. No hostile-runtime/sandbox claim: this is ordinary Python object aliasing between registered callbacks.

One authored case: reservation0.02, limit0.05, billing instrument returns the same valid provider_reported0.06 object; interpreter changes that object's money field to0 and returns valid finite JSON. Criterion: durable saved fee0.06 must remain independent of interpreter mutation; either settle0.06 explicitly or prevent semantic object mutation from affecting accounting before receipt. Raw preserved, exact first usage artifact, ledger/result/receipt exception retained. Compare saved billing and ledger accounting rather than interpreting integrity rejection as accounting correctness. No payment, keys, producer edits or real source analysis. Synthetic raw/journal archive captured in finally.

## Exact same-case corrected-source retest32

Original29 fee0 plus late integrity rejection remains unchanged. Current adapter reloads durable first usage into a detached JSON object before interpretation. Only source and explicit external-parent lineage change; same raw, shared mutable instrument return, semantic mutation, reservation/limits and criteria are reused.
