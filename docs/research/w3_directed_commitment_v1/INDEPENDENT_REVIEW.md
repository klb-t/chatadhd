# Independent review — W3

Reviewer: separate `w3_independent_cases` agent. Fixtures were authored before
implementation inspection. This is an independent author/reviewer within the
same agent session, not an external blind model-quality audit.

The reviewer authored 12 cases / 48 queries and then found three concrete issues:

1. Independent DEV gold was not part of preparation's hash closure.
2. Empty freeze inventories were accepted; a changed planned count could pass.
3. Direct historical replay could not find loose raw files stored in the archive.

Repairs: hash labels without loading their semantic contents during preparation;
require exact freeze inventories plus reconstructed counts/rows/digests;
verify and restore the original archive into a new temporary directory. Both
test files are frozen. The reviewer independently verified exact replay of
both original response sets and existing-schema validation of the two profiles.

Final reviewer signoff: 9/9 independent tests pass; neither independent request
arm contains gold-only fields or any of the 48 rationale strings. Earlier
blocking findings are resolved. New-recipe model quality remains unmeasured.

The integrator subsequently marked the historical comparator replay-only,
disabling its duplicate live request plan. The final combined 20-test suite
includes an explicit no-repeat check. Tests preserve missing denominators,
refuse mismatched recipes and corrupt archive containers/members, and exercise
the freeze fixes. These tests establish mechanism behavior, not semantic model
performance.
