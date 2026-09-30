# AnalysisPlan provenance boundary: protocol 15

2026-09-30. Independent authored fixtures only. Deep concurrency/ledger review
belongs to the separate validation agent. Capture an immutable source snapshot
and schema/validator hashes before outcomes. No real provider or graph write.

Cases: unchanged caller semantic contract/source provenance arrives intact;
stable unpinned source reference first resolves packetA and later packetB;
reported zero with null measurement provenance; reported zero with empty object
provenance; absent money measurement retains its declared reservation; declared
tool configuration/capability remains configuration rather than an engine tool
dispatch; an explicitly pinned source revision changes the plan/attempt identity.

Hypotheses: cached results currently do not establish that an unpinned reference
still resolves to identical input bytes, and key presence alone may accept null
measurement provenance as measured zero. Preserve such outcomes as diagnostics,
not as evidence that a callback actually spent money or violated permissions.
Caller registration can intentionally delegate execution; do not turn this
offline reference executor into an unsolicited universal permission barrier.

Expected valid controls: native input/known_at and semantic contract preserved,
unknown cost remains held, a changed explicit revision gets a distinct attempt,
and the engine never calls tool handlers or applies graph proposals. Candidate
fixes should use data policies/adapter contracts for source binding and quantity
status rather than require one domain, database, model or acceptance workflow.
