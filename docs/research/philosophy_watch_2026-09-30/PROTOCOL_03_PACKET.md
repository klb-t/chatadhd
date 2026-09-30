# Independent graph-packet mechanism audit: protocol 03

2026-09-30. Audit the packet producer's actual code in an immutable local source
snapshot before inspecting outcomes. No production edits, canonical graph writes,
API calls, secrets, owner archive or sealed fixture reads. Native-shaped records
below are newly authored mechanism fixtures, not model outputs.

Check roundtrip byte/field fidelity, preview versus automatic/explicit acceptance,
origin/known_at retention, source immutability and configurable tombstones,
reversible diffs, stale-base/CAS rejection, semantic/nonworldtruth boundary,
unrestricted-by-default versus caller-bounded resources, open definitions/actions,
large finite budget in task data, and source/support/reference coherence.

Also challenge imported history: a correct content hash authenticates neither a
person nor a model, and must not conceal a structurally invalid diff/change list.
First failures remain in the first result and source snapshot. The code owner may
then fix independently; retests get a new result/snapshot. Tests certify neither
semantic claim correctness nor native-store import, calibrated confidence or
frontier-model performance.
