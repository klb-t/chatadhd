# Conditional W1 semantic / actual W2 ledger proof

The native offline fixture passed **5/5** cases with **0 paid calls**.
`semantic_w2_conditional.json` pins the current W1 source hashes, exact public
W2 Git blobs, compiler, object hashes, root archive and link order.
`semantic_w2_conditional_raw.json` preserves the full synthetic API and ledger
receipts. No fixture source was promoted to a production W2 file.

The fixture compiles W1 `semantic.cpp` against the actual W2 header first on
the include path, and links its object plus the original W2 policy/config
objects before W1's normal core archive. Runtime capability reports available.
It installs the exact W2 default options as explicit caller config because W2's
Config getter defaults and C ABI are still outside this source-level test.

The cases cover cold admission; initial input-byte baseline 1 requiring owner
confirmation before any HTTP; correct receipt confirmation and one same-run
dispatch followed by cache resume; reported output actual 2 settling the token
baseline; and absent token/cost usage remaining unknown. The configured output
cap 1600 stays provenance and does not become an invented usage forecast.
Exactly-once evidence covers the tested checkpoint/cache path, not concurrent
dispatch locking. All model responses use ScriptedTransport; their usage/cost
values are fixture inputs and do not represent paid provider spending.

Reproduce from a repository version with the W1 source hashes in the summary,
a completed normal `loom/build/dev` archive, and the pinned W2 Git commit
available locally. Run the three sequential commands in `replay_commands`.
Use a new scratch directory/result filename for each attempt. No tests, limits
or unrelated production files are modified by the helper. After actual W2
integration, the normal merged build and full CTest remain required.

Earlier source-race and linker resource failures executed zero fixture cases
and remain separate scratch infrastructure history. They are not graph/model
quality evidence and are not represented as successful integrations.

The original W2 ledger also sets `overrun=true` when a null token/cost estimate
is settled to a known actual. The raw receipt retains this diagnostic; it is
not evidence of an expected consumption increase or a new confirmation rule.
This behavior belongs to W2 and was not changed by the fixture.

Fresh rebuild after the generated runtime-disabled W7 export passed **5/5**
on full semantic source SHA `c9e844ff65d87e3345ed51f2bdef5500724e7a781fa0912b19b1016143463db5`. The earlier
`0acd0996` proof remains in the prior scratch directory; it was not reassigned
to the new full source hash. The source body after the generated prefix is
byte-identical, and both generator products passed `--check`.

The final root archive, including legacy analysis public headers, was relinked
with these unchanged W2/semantic fixture objects and again passed **5/5**.
The summary records its final archive/executable/raw hashes separately. The
previous two proofs remain in scratch; no objects were recompiled for this
last pass.
