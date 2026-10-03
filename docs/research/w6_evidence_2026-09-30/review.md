# Independent review of W6 import evidence

Reviewed 2026-09-30: `loom/tools/w6_evidence/import_audit.py`,
`real_export_audit.py`, their public protocols, synthetic receipt and supplement,
and public real-export aggregate receipt. Reviewer did not author either runner.
No private input, runtime, stdout/stderr, real conversation content or protected
holdout was opened. No native import was rerun and no runner was modified.

## Verdict

The evidence supports the stated narrow successes and preserves consequential
negatives. It does not support a blanket lossless-import claim. In particular,
the synthetic Anthropic array-order failure remains visible despite preserved
message values and raw ZIP bytes. The real OpenAI shard's aggregate reports
100/100 exact reconstructed conversations, 730/730 compared raw messages,
9020/9020 typed atoms, zero missing/changed/added atoms, and an equal source blob.
These real-input results were reviewed through the scorer and aggregate only,
not independently reproduced against private contents.

## Findings and follow-up suggestions

| Priority | Finding | Consequence / suggested next instrument version |
|---|---|---|
| Medium | Synthetic `structured_leaf_positions` accepts `not (before - after)`, without rejecting `after - before`. | Added structured values can pass. Preserve the frozen result and add an explicit added-atom check in a versioned scorer. |
| Medium | Synthetic `leaves` encodes both an array index `0` and an object key `"0"` as `/0`, while omitting nonempty container types. | `{'x':['a']}` and `{'x':{'0':'a'}}` compare equal, including in its raw-message comparison. Record typed container/path segments or nonempty container markers. The real runner's tuple paths distinguish integer indices from string keys and avoid this specific problem. |
| Medium | Real runner creates the output directory with `exist_ok=True`, writes `real-export-before.json` before creating the fresh runtime, and overwrites the final result filename. | A repeated invocation can overwrite first-run evidence; even an existing-runtime failure can replace the before-manifest. Freeze these existing files and use exclusive output creation for future runs. The synthetic runner already refuses an existing result directory and verifies its manifest. |
| Interpretation | Real locator count asks only for `json_pointer`; synthetic asks for `json_path` and an Anthropic source ordinal. Current native provenance contains `conversation_index`/`message_index`, plus ZIP member when applicable. | The real 0/730 is **zero explicit JSON Pointer fields**, not zero recoverable source associations. OpenAI metadata retains `export.key`, allowing reconstruction with source conversation and mapping key. Provenance alone lacks that exact pointer. Future instruments should distinguish pointer presence, composite-key resolution, original-array ordinal and traversal ordinal. |
| Interpretation | Real `message_status_counts` describes native results; it has no independent current-branch/parent oracle. Real reopen/reimport compares message metadata and row counts, not all conversation metadata or statuses. | Do not call these fields real-data branch-classification validation or comprehensive persistence validation. Synthetic fixtures independently check branch statuses and parent IDs. Extend only the missing claims when needed. |
| Interpretation | `provider_requests: "not performed"` and the network-mode label are assigned by the real runner, not measured by an intercepting transport or network observer. | The fresh runtime contains no credentials and explicitly disables semantic analysis/model, with a loopback base URL; that supports the intended offline configuration. It is not a counted network receipt. Describe this as configured offline execution, separately from the transport lane's observed requests. |
| Instrument robustness | Real runner records process return codes but proceeds to inspect SQLite after import failure; both runners can abort before writing a complete receipt on unexpected result/schema errors. | For future versions, save a sanitized failure receipt before raising. Preserve private diagnostic output in private runtime; do not copy it to public evidence. This is a future failure-path limitation, not evidence that the successful saved run failed. |

The synthetic wrapper-field check tests the presence of `unknown_wrapper`
somewhere in metadata rather than its exact path and value. Its current failure
is valid, but a future success would need a stronger reconstruction test. The
real runner is explicitly scoped to a conversation array, not provider wrappers.

## What was independently checked here

The synthetic fixture manifest's referenced file hashes all match. Recounting
the saved checks yields **47 passed / 59 named conditions**, including oracle
controls; this is not an accuracy estimate or 59 conversations.

Two new, tiny synthetic scorer probes were run by importing `import_audit.py`
without executing its CLI:

```python
before = set(leaves({'x': 1}))
after = set(leaves({'x': 1, 'injected': 2}))
assert not (before - after)  # current condition accepts the added field
assert set(leaves({'x': ['a']})) == set(leaves({'x': {'0': 'a'}}))
```

These reveal scorer blind spots; they do not change the original negatives.
Source inspection of `export_anthropic.cpp` confirms message metadata retains
the raw message and parent/key facts but does not record the original array
ordinal in that message's export metadata. Its reordered traversal and the
saved synthetic locator evidence explain why an ordinal can resolve the wrong
source message. The test measures reconstruction from structured fields; the
original order remains recoverable by reopening the retained raw archive.

The two `member_bytes:conversations.json` failures mean no separate member blob
was found by that frozen check. They do not establish lost bytes when the
complete stored ZIP is equal. `import_supplement.json` correctly separates
recovery from the stored ZIP from the original direct-member-blob condition;
it must not replace or retrospectively flip those first-run results.

## Public-output and credential boundary

The real runner's successful public output contains source/binary/output
hashes, counts, fixed labels and booleans; it does not serialize source text,
message IDs, local source paths or private CLI stdout/stderr. Diagnostic files
are written inside its newly created mode-0700 runtime, and its config file is
mode 0600. No credential value is read by this Python runner. A source hash and
structural counts are still identifying metadata, not anonymization; the
protocol explicitly selects them as the public receipt.

The synthetic runner intentionally records full message rows, provenance,
stdout/stderr and paths. That is suitable for its fixed, synthetic fixtures;
it is **not** a safe drop-in real-data reporter. No real-data contents were found
in the reviewed aggregate schema or intentionally loaded for this review.

This review leaves source truth, real attachment-byte resolution, representative
sampling, full-archive scale and model comprehension unevaluated.
