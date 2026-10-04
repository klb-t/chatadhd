# Effective catalog relevance recipe

Catalog scoring reads `policy/relevance.json` from the already resolved KB
pack. The existing `<data_dir>/kb/policy/relevance.json` file overlay replaces
that complete document. This change adds no second loader or configuration
store, and does not change the built-in policy.

The checked decoder consumes `bias`, `weights`, `term_class_weights`, `channels`
and all three `bm25` fields (`k1`, `b`, `normalise_percentile`) from data.
Missing or malformed values produce an explicit error before score/link
mutation. Weighted features require a declared channel; channel and feature
names remain open. Numeric weights and bias have no new magnitude ceiling in
the decoder. BM25 requires `k1 >= 0`, `b` in `[0,1]` and percentile in `[0,100]`.
These are parameter domains, not quality gates. The existing KB pack validator
still imposes its own bias/k1 bounds; removing those belongs to thread 4.

The expansion class weight is required by scoring and `add_profile_terms`.
Both now use `term_class_weights.expansion`; there is no separate `0.8` preset
in these consumers. BM25 no longer restores C++ literals when an overlay omits
a value. Custom overlays that relied on those omissions must supply the fields.
All built-in DEV scores, labels, features, reasons and decisions remain equal.

The existing score API adds `relevance_recipe` to its summary:

```json
{
  "parameters": {
    "bias": -3.0,
    "weights": {},
    "term_class_weights": {},
    "channels": {},
    "bm25": {"k1": 1.2, "b": 0.75, "normalise_percentile": 99.0}
  },
  "sha256": "<canonical effective-parameter hash>",
  "document": "policy/relevance.json",
  "pack_hash": "<resolved pack hash>",
  "resolution": "already resolved pack including file overlays",
  "graph_persistence": false
}
```

The empty maps above abbreviate the full maps returned by the API; this is not
a replacement policy example. `channels` in the receipt is feature-to-channel,
while the policy uses channel-to-feature arrays. Schema/description metadata is
excluded from the parameter hash. The existing run fingerprint already binds
the resolved pack, so adding this inspection receipt does not change run IDs.

This JSON receipt is not graph provenance, a complete immutable run artifact
or an explanation of individual overlay layers. Consume the canonical
`loom.method_graph/1` / `loom.method_run_trace/1` contract from threads 3/4
when their common implementation is accepted; do not invent another vocabulary.

For DEV experiments, `tests/evaluate_dev.py --relevance-overlay FILE` installs
the complete saved policy via the same native overlay and records the exact
policy, loaded policy, pack/source/fixture/binary hashes and unchanged aliases.
No provider calls or vectors are needed for intercept calibration. A DEV-only
calibration is not evidence of independent model quality and does not promote
the built-in default.
