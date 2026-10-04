# Supplied semantic candidates before archive selection

The existing `loom_catalog_score` C ABI now consumes the optional runtime setting
`catalog_semantic_candidates`. Use `loom_set_config_json` to set it. This is an
offline replay/input boundary; it does not contact a provider. A producer can
supply saved embeddings without requiring aliases or lexical retrieval to find
the units first.

First score with the setting absent/disabled. The score result returns
`profile.id` and the complete `profile.input_hash`. `loom_catalog_query` returns
the unit IDs and exact catalog-record `content_hash` values. That hash binds
the scanner's record representation, which depends on the source format; it
does not by itself bind the complete raw export or the producer's text
projection. Retain raw-source and embedding-input hashes separately. A producer
binds its vectors to the returned catalog values, then supplies this envelope:

```json
{
  "catalog_semantic_candidates": {
    "schema": "loom.catalog_semantic_candidates/1",
    "enabled": true,
    "profile_input_hash": "<complete hash returned by score>",
    "channel": "owner-selected-channel",
    "model": "<exact model/revision selected by the producer>",
    "method": "<producer's embedding method/revision>",
    "query": [1.0, 0.0],
    "records": [
      {"unit_id": "<catalog ID>", "content_hash": "<exact record hash>", "vector": [1.0, 0.0]}
    ],
    "policy": {"bias": -3.0, "weight": 8.0, "tau_relevant": 0.7, "fusion": "union"}
  }
}
```

The sample vectors are artificial. Bias, weight, threshold, channel, model,
method, vector dimensions and records are caller data; the example is a preset,
not a calibrated classifier. There is no arbitrary vector/record count ceiling.
Malformed dimensions, nonfinite/zero vectors, duplicate IDs, unknown units and
stale profile/content hashes fail before score/link mutation.

The rank is `sigmoid(bias + weight * cosine(query, unit))`. It is a retrieval
rank, **not calibrated confidence or proof of project membership**. Provenance
retains hashes of the envelope, query, unit vector, source record and profile,
plus the declared channel/model/method. Those declarations are not independently
verified claims about the producer. No canonical entity, claim, identity alias
or project assignment is created by a vector hit.

`union` keeps an independently qualifying semantic hit relevant without a
lexical endorsement. `additive` combines its log-odds with the existing unrounded
logit; lexical penalties can then affect the result, as chosen by the caller.
Rounding happens after fusion, so printed endpoints do not create extra caps.
Union does not demote an existing lexical hit on a negative semantic rank.
Owner overrides remain first, followed by the existing explicit selection rules
and the resulting label. A tiny candidate rule does not discard a unit whose
semantic channel independently made it relevant. Explicit rules can still
exclude a relevant unit. Unprovided units receive no score from the channel,
even with positive bias. Disabling the channel restores the original run ID,
features and decisions when the other inputs, profile, pack, corpus and owner
overrides remain unchanged. A changed active envelope gets a different run ID.

`external_semantic` in the score summary reports availability, supplied/missing
units, method/model/configuration hash, fusion and zero provider calls.
Availability means that a supplied-vector envelope was accepted; it does not
report a working producer or a reachable model service.
`channels.external_semantic` records lexical-only and semantic-only unit IDs;
it distinguishes missing semantic evidence from a scored result below the
channel threshold. These gaps need independent verification before being
called lexical rescues or noise. Existing word/character TF-IDF diagnostics
remain separate: their shared lexical features are not independent dense-model
evidence.

The producer must obtain authorized vectors and account for its own provider
usage. Wiring the production provider/cache, the shared usage policy or UI
controls is outside thread 6's file scope; this change neither spends money nor
establishes model quality on real archives.

The current provenance is JSON with input hashes and unverified method/model
names. It does not yet implement the owner's graph entities for methods,
versions and runs or the result-to-method provenance edge. Threads 3 and 4
must agree on that shared contract before graph persistence is wired. The run
identifier does not bind the complete unit/sketch corpus, and the score tables
are not immutable history. Persisting the full input envelope and output
artifacts is still required for that contract.

Owned-path regressions are reproducible separately from unchanged CTest:

```sh
python3 loom/src/catalog/tests/evaluate_dev.py --library loom/build/dev/libloom.so.0.1.0 --output /tmp/catalog-dev.json
python3 loom/src/catalog/tests/test_catalog_replay.py --help
```

The replay regression uses explicitly label-derived synthetic vectors to test
plumbing for 14 recorded DEV omissions. It must never be reported as model
recall improvement. The five verified lexical rescues and all 20 noise cases
stay in the regression population.
