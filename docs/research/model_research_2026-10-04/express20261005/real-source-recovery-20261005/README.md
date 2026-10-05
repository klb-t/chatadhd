# Real-source intake — 2026-10-05

This offline increment continues the shared source handoff for threads 7A/7B/7C.
It does not dispatch models, score their quality, replace historical freezes, or
claim a native GraphPacket store integration.

## Recovered and verified

The owner-supplied private Library capsule is
`Thread7_REAL_SOURCE_FREEZE_20261005.zip`, SHA-256
`5f976bbf9cee15dfe232e329dc2c85ebc40cae981af7ceedc56fc8e08719a832`.
The ZIP and its original files remain unchanged. All 13 members referenced by
its previous 7A freeze verified, including the eight prepared operations.
All three original-conversation canonical hashes verified separately from raw
file hashes. These are three selected OpenAI conversations, not the complete
Takeout corpus, an Anthropic sample, or a representative holdout.

## Defect and additive correction

The source `messages-full` array is not conversation chronology. Two of three
cases place child messages before their parents: 9 and 11 inverted direct
message edges, respectively. The previous model context copied that array,
without the parent links or source times needed to interpret it as a graph.
This is a source-preparation defect; no model-quality consequence was measured.

`intake.py` validates the immutable capsule and adds a separate private graph
view. It preserves all 44 native messages, their roles and existing turn IDs,
47 saved nodes, all 44 parent edges, recorded times, saved branches and the
current-node ancestry. It derives a topological view from native parent links;
sibling order is explicitly not asserted as chronology. Contradictory declared
children, missing parents, cycles, changed source bytes and coverage gaps fail
closed. Null times remain unknown. Neither message text nor IDs are regenerated.

The new view is NOT substituted into any old request body. The original eight
operations and all old hashes remain historical and unchanged. Any corrected
request population needs a separate version/hash and preflight; replay must
state which source projection was used. Existing paid attempts are not retried
or invalidated merely by this intake finding.

## Verification

`VERIFICATION.json` binds the exact producer, tests, data layout, complete log and
public receipt. **37/37 unittest cases, zero failures/skips**, plus an actual
private-capsule verification with all 44 derived parent edges forward and exact
message/turn-ID coverage. Unit fixtures are mechanism tests, never synthetic
model experiments. New paid calls: **0**, new spend: **0 USD**. No native CTest,
web build or hosted Actions run is claimed for this isolated research increment.

## Reproduce

Python 3.10+ and its standard library suffice. Tested with Python 3.13.5.
Run from this directory:

```sh
python3 -m unittest discover -s . -p 'test_*.py' -v
python3 intake.py \
  --capsule /PRIVATE/Thread7_REAL_SOURCE_FREEZE_20261005.zip \
  --expected-sha256 5f976bbf9cee15dfe232e329dc2c85ebc40cae981af7ceedc56fc8e08719a832 \
  --layout layout.json \
  --private-output /PRIVATE/new-intake/graph-view.json \
  --public-receipt /PRIVATE/new-intake/receipt.json
```

Use new output paths: neither file is overwritten. The private output is refused
inside a Git working tree and is written with mode 0600. Do not put the capsule,
private view, full requests, ledger or response text in this public repository.
Archive members are verified in memory; no extraction or provider call occurs.

## Public projection and remaining gates

`REAL_SOURCE_RECEIPT.json` is an explicit allowlist of technical hashes, counts
and status. It contains no original titles, message contents, source IDs,
locators or timestamps. It is not a public full replay. Content hashes are
integrity references, not anonymization of a published raw corpus. The complete
private view is reproducible only with authorized access to the private capsule.

For 7B/7C the shared source-intake gate is now satisfied. Reviewed semantic gold,
new request freezes and the sole central 7A payer's current cost/ledger preflight
remain open. Three sources cannot be reported as the old 24-family / 12 PL +
12 EN design. Do not translate or fabricate missing families, inherit synthetic
gold, claim unseen evaluation, or alter the frozen classifier threshold.

The parent `../COORDINATION.json` is a handoff/status document, not an executable
dispatch authorization. Stop synthetic model dispatches; preserve archived
synthetic fixtures and all first responses. No quality or new model ranking is
claimed here.
