# W8 follow-up — DIC-0692 seeding profiles

The seeding prototype now reads graph dimensions and project-field projections
from `loom/tools/seeding/profiles/default.json`. Ranking policy was already data;
the dimension registry, projection fields, expected-property templates and
premise bindings were previously Python branches. They are now profile data
executed by generic operations in `recipe.py`. No `loom/data` file, historical
result, frozen historical script or existing test was changed.

The public functions and CLI accept a chosen profile and ordered overlays.
Objects merge recursively; arrays/scalars replace, with no implicit deletion.
Resolved stages validate before execution. Unknown executable declarations
raise `RecipeUnavailable`; opaque extensions and exact input bytes survive.
Dimension and row counts have no invented ceiling. The four existing ranking
algorithms retain their behavior; selected methods, budgets and random seeds
are honored by the experiment and CLI.

Premise eligibility refers to declared dimensions and configurable property
bindings. Renaming capabilities/principles and all ten eligibility fields works
without Python changes. Alternatives remain ORs of complete, nonempty
conjunctions. Masked target properties/prose do not reach the predictor. Source
creation dates remain separate from unknown source know-times, and candidate
creation time is explicit. No proposal gains observed status through a profile.

Arbitrary names remain distinct: typed score groups separate project and
dimension identities even when called `all`; case IDs escape slash separators.
Property keys escape colon/percent only in the dimension prefix, preserving
every historical label byte, including the two V4 feature labels containing
colons. The shapes `(a:b, c)`, `(a, b:c)` and `(a%3Ab, c)` have distinct keys and
retain their own properties and prediction witnesses.

New runs use `candidate-profile-projection-v3` and retain exact fixture, policy,
protocol, base profile, ordered overlays, effective profile and all runtime
modules/default/schema bytes with SHA-256 receipts. A frozen CLI replay succeeds
after the original inputs are deleted, from another working directory without
`PYTHONPATH`. The shared method-graph bridge is explicitly
`pending_agreed_contract`; this change introduces no competing graph format.

| Check | Before | After |
|---|---:|---:|
| Default prediction/support dimensions | 3 / 1 in Python | Same 3 / 1 declared in profile data; configurable registry |
| Historical graphs matched | 5 | 5 exact |
| Development cases | 46 masks + 15 controls | Same 61 exact |
| Fixed-time ranked outputs | 2,135 saved V4 records | 2,135 exact matches |
| Complete metric dictionaries | 648 saved V4 dictionaries | 648 exact matches |
| Existing tests | 36 | Same 36 pass unchanged |
| New profile/configuration tests | 0 | 14 pass |
| Historical result files/bytes | 26 / 13,800,361 | Unchanged 26 / 13,800,361 |
| Paid calls / canonical graph writes | 0 / 0 | 0 / 0 |

The final combined suite executed **50/50 cases in 7.922 s**, with its actual log
and SHA-256 receipt. Default and composed demonstration profiles pass the
declared Draft 2020-12 JSON Schema. The independent tests include frozen graph
and ranking reconstruction, malformed/unavailable declarations, fully renamed
eligibility bindings, hidden-target redaction, conjunction alternatives,
collision cases, a method subset, nonzero seeds, unavailable empty-seed
expectation and complete frozen-run replay.

[Reproducible verification and hashes](../verification/repo-hygiene-followup-2026-10-04/seeding/README.md)
retain the existing V4 baseline rather than duplicating multi-megabyte ranking
data. The small complete new run is six synthetic cases with budget 2, seeds
7/8 and two ordered overlays; its exact decompressed outputs are UTF-8 JSON.
No new quality improvement is claimed. Existing negative results remain intact.

Full native CTest is the root W8 verification step after this source increment;
this subtask ran the complete seeding suite and the explicit baseline replay.
Only `loom/tools/seeding/**`, the assigned follow-up verification directory and
this report were edited. No commit, branch change, paid call or sealed/blind
corpus access was performed by this subtask.

## Do wątku 9

Track DIC-0692 as implemented with exact default equivalence, subject to the
full branch verification recorded by root W8. Preserve every historical run.
After W3/W4 agree their contract, pass it to W8 for the method-graph adapter:
profile version/effective hash, ranking-policy hash, runtime hashes, selected
method/parameters and run/result identities are already retained. The pending
bridge is not a second graph protocol or completed graph persistence.
