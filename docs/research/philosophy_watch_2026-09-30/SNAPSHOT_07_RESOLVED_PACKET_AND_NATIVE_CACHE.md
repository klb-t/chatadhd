# Latest source-packet and nonempty cache checks

Known at 2026-09-30 13:45 UTC. Current AnalysisPlan source
SHA2dfc9c3c67fb7135477b91e5599bbc30c96e006ab301b55b4a1d0f142f442372
passes the one-case23 resolved-input receipt check. Before the callback can
mutate its private argument, the exact original Unicode packet envelope is
retained. Context, dispatch and bound replay explicitly state
`caller_declared_not_verified`. Raw packet and resolved-envelope digests remain
separate. Replay executes no second loader/callback and rejects tampered retained
input as `result_receipt_hash_drift`. Different source/graph semantics are allowed;
this is input preservation rather than a generic source fingerprint proof.

First harness22 incorrectly expected a raw packet where the producer artifact
contained `{loader_supplied, packet}`. Its callback expectation and later digest
assertion failed, and the temporary context cleaned incomplete artifacts before
capture. This limitation is explicitly recorded in
PLAN_LOADED_PACKET_INCOMPLETE_HARNESS_22.json; original22 source/script/freeze
remain unchanged. No producer defect or model-quality result is attributed to
that harness error. Separately named23 corrects only envelope expectations.
Future probes should archive temporary evidence in a finally block before test
assertions can trigger cleanup, as robust production first-attempt runners do.

Independent cache spot24 passes6/6 on a distinct authored nonempty native fixture:
Unicode source, two Entities and a source-supported Claim. Coupled Claim/source
tombstone and restoration use the authorized immediate parent and preserve exact
strict application receipts. Removed raw source/Claim bytes, original provenance
and source known_at survive in history. Restored native archive assessment origin
stays distinct from model instrument provenance; configured automatic acceptance
does not establish content truth. Stale inverse and rehashed retained-before raw
corruption reject equally in cache/strict validation. A valid competing proposal
over the same parent retains its distinct identity instead of forced consensus.

Cache source remains bb6ac98489a85a807eb2879d877943c2f38843d852c8ffe1aaa4d0a67e225425.
Producer reports198 timing/66 memory/66 exact-equivalence observations in its
separate resource matrix. This audit has not recounted the timing/memory outcome
table and makes no performance conclusion from that report. Native spot checks
are structural/provenance mechanisms, not semantic source-grounding certification.
Actual paid requests, model inferences, holdout reads and canonical writes here0.
