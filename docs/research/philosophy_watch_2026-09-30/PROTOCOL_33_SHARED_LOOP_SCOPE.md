# Local scope allowance memo after semantic rejection

Registered before one scripted execution; suggested from reading runtime_loop control order, not observed outcome. A leaf can have valid completed accounting but `state=semantic_rejected`. Loop currently checks this state before its local scope charge. Hypothesis: it returns local retained allowance0 despite a known completed provider_reported fee0.06 in shared ledger. This does not imply another dispatch or a shared-budget bypass; the loop stops. It tests whether the additional local memo accurately reflects the selected retained allowance.

Freeze preserved initial adapter round28 plus current runtime_loop. Execute one minimal declared local model callback with reservation0.02, shared limit0.05, local scope1 and valid cost0.06 in raw JSON lacking required runtime semantic fields. Raw billing should settle independently; semantic interpretation rejects. Criterion: exact raw retained, one callback, shared usage0.06 and local scope allowance0.06 (or explicitly scoped alternate measure that does not claim retained allowance). Preserve raw/journal/loop receipt in independent ZIP. No network, model, key, producer edit or validation/holdout.

## Same-case corrected-source retest33

Original30 semantic-rejected fee0.06/localmemo0 remains unchanged. Producer reordered accounting before semantic-state break and includes uncertain started reservation. This repeats the one exact raw/request/reservation/cost case on current adapter/runtime_loop with explicit caller-declared external parent label; no numerical/semantic criteria changed. Uncertain/new concurrency paths are outside this one-case result.
