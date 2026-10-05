# Exact repetition request capsule

`repetition-request-replay-v1.zip` preserves all current bytes in
`source-manifest/**` and `prepared/**`, with an internal hash/size inventory.
Its 530 payload files plus inventory contain the public equivalent of the original
432-request source and the new 192-operation repetition plan. Policy, selection,
gold, helper and tests remain separate public, hash-bound repository inputs.
There are no actual replies, keys, account metadata, private ledger or binaries.

The verification receipt records a clean CRC, every member hash/size, a clean
extraction followed by source-bound `verify`, and byte-exact `prepare` reproduction
of all prepared artifacts. The ZIP replaces hundreds of duplicated request files
in publication; local originals remain available to the root coordinator.

Restore to a new caller-owned directory:

```sh
unzip docs/research/model_research_2026-10-04/stage5/label-repetition-v1/repetition-request-replay-v1.zip \
  -d /tmp/label-repetition-replay
python -m loom.tools.structure.programme_repetition_v1 verify \
  --source-manifest /tmp/label-repetition-replay/source-manifest/manifest.json \
  --selection docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/selection.json \
  --policy docs/research/model_research_2026-10-04/stage5/label-repetition-v1/repetition-policy.json \
  --manifest /tmp/label-repetition-replay/prepared/manifest.json
python -m loom.tools.structure.programme_repetition_v1 prepare \
  --source-manifest /tmp/label-repetition-replay/source-manifest/manifest.json \
  --selection docs/research/model_research_2026-10-04/stage5/stage1-exploratory-v1/selection.json \
  --policy docs/research/model_research_2026-10-04/stage5/label-repetition-v1/repetition-policy.json \
  --output /tmp/label-repetition-reproduced
```

These commands make no model calls. They preserve the exact prepared request
bytes and immutable origin bindings; live pricing, key/cumulative budget admission
and paid dispatch remain with the existing root executor.
