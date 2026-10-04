# W1 — rejected prompt experiments, 2026-10-04

This appendix and its complete source/receipts belong only on the negative
archive branch. They do not change production source, accepted tests or gates.

The ordered-JSON oracle originally reported **15/16 cases and 240/242
assertions**. Two assertions compared insertion order rather than identical
JSON fields/values. Its exact source and full original stdout are preserved.
The accepted fixture uses canonical content equality plus exact original
raw-response bytes; no native validation or threshold was relaxed.

The first legacy shared builder retained **3/3 exact prompt contents and 3/3
JSON request semantics**, but only **0/3 exact request wire bytes**. Its exact
recovered effective definition has canonical hash
`fb31e66cf09406d0f29ab1bad19290507c3df67b59e63785824788fde849dac2`.
The original three-input receipt retains every old/generated body hash.
The missing wire-order preset and the historical ineffective preservation
toggle remain archived, rather than silently replaced by the accepted preset.

The source-level replay reads the recovered definition via `from_snapshot`.
A complete private registry copy admits only the historical
`preserve_unknown_fields: true` metadata and is linked before the matching core
archive; no production validator is loosened. This reconstructs the original
negative body/hash outcome, not a claim that a modern copied implementation is
the vanished historical binary. The static reference bodies and linked
`kAnalysisPrompt` are unchanged. Successful replay means rejection reproduced.

Sources, first receipts, precise registry patch, standalone replay script and
hash manifest are under `loom/src/extract/tests/archive/2026-10-04/`.
Actual standalone replay reproduced both negatives: **15/16 cases, 240/242
assertions** for the original oracle, and the **entire historical fb31 receipt**
with **0/3 wire bytes, 3/3 semantics, 3/3 contents** for the legacy builder.
All three compile/link steps exited 0; the oracle deliberately exited 1. Full
stdout/stderr and source/library hashes are saved in `replay-evidence/`.
No paid or live provider calls are made. Compile/link infrastructure failures
with zero executed cases remain distinct from the measured negative outcomes.


## Do wątku 9

Keep this archive branch outside the accepted main tree. Use the replay script
to reproduce the two measured negatives; infrastructure logs are diagnostic
history and not additional model-quality results.
