# Seeding profile migration evidence — 2026-10-04

All inputs are fictional, inspected synthetic development data. These checks
measure implementation equivalence and configuration behavior; they establish
no independent, temporal, semantic-extraction or model-quality result.

`golden-proof.json` records exact reconstruction of five historical V4 graphs,
61 masks/controls, 2,135 ranked outputs and 648 complete metric dictionaries.
The historical V4 results remain the complete baseline in
`loom/tools/seeding/results/synthetic_lopo_v4_application_alternatives_first/`.
The proof does not duplicate their multi-megabyte JSON. Both canonical ranking
matrix digests are identical. The script additionally checks all 26 historical
result files against the first cleanup inventory; all 13,800,361 bytes remain.

Reproduce the comparison from the repository root:

```bash
python3 docs/verification/repo-hygiene-followup-2026-10-04/seeding/verify_profile_migration.py
python3 -m unittest discover -s loom/tools/seeding -p 'test_*.py' -v
```

`unittest.txt` is the actual combined output. `unittest.json` records its hash,
command, elapsed time and 50 executed cases: the unchanged original 36 and 14
new profile/configuration regressions. These include arbitrary dimensions,
all configurable premise bindings, conjunction alternatives, target redaction,
unavailable operations, malformed profiles, group/property/locator name
collisions, method subsets, nonzero random seeds and an input-independent frozen
CLI replay. Both the default and composed demonstration profiles were also
validated against `profiles/schema.json` with Draft 2020-12 JSON Schema.

`synthetic-inputs/` contains the six exact inputs to a small six-case
demonstration. `synthetic-new-run/` preserves the complete frozen runtime,
default/schema, exact base with BOM, two ordered overlays, effective profile,
fixture, policy and protocol. The dimension and one project intentionally share
the name `all`; typed score groups stay distinct. Budget 2 and random seeds 7/8
exercise configuration beyond the previous implicit budget-1/seed-0 assumptions.
All four existing ranking operations are selected without changing their
algorithms. This is a smoke/replay receipt, not a quality experiment.

The exact decompressed predictions/results are stored as UTF-8 JSON for textual
publication. `synthetic-replay-proof.json` records the original runtime gzip
hashes, lossless conversion, exact score-group comparison and replay command.
`synthetic-cli-replay.txt` is the actual frozen-CLI output. There are no new
binary gzip files in this proof directory. Runtime runs still use deterministic
gzip; the original historical gzip/source bytes were not changed.

From this directory, replay with an unused output path:

```bash
python3 synthetic-new-run/modules_frozen/prototype.py --output /tmp/seeding-profile-replay \
  --fixture synthetic-new-run/fixture_frozen.json \
  --policy synthetic-new-run/policy_frozen.json \
  --protocol synthetic-new-run/protocol_frozen.md \
  --profile synthetic-new-run/profile_frozen.json \
  --profile-overlay synthetic-new-run/profile_overlays/000.json \
  --profile-overlay synthetic-new-run/profile_overlays/001.json
```

The profile/hash receipt is separate from the pending shared method-graph
contract. No method-graph bridge, paid call or canonical graph write is claimed.
