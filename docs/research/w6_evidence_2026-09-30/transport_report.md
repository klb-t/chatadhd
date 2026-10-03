# W6 independent transport result

Final attempt 02: **72/72 behavioral checks passed**, covering eight native chat
attempts and six requests received by an independent loopback HTTP server. This
is not 72 independent experiments. Five received requests have traces; one
explicit opt-out has none. Missing-key and missing-run attempts never reach the
provider. Six stored traces (including a compiled but unsent missing-key trace)
survive both native-process reopen and semantic reindex: **6/6 each**.

Native baseline: `b118c80e981c08ec6d7f9ab6aacc177979186cf2`, clean source checkout.
Existing continuation binaries were reused read-only. `git diff --stat
a79862837c6726f534471f8dbcdccfe749dfb942 b118c80 -- loom` is empty, consistent with
the earlier native build receipt. No production code was modified here.

| Boundary | Observed result |
|---|---|
| Source to knowledge | New synthetic export processed by offline native knowledge run; no provider calls during ingestion |
| Knowledge to prompt | Explicit native entity IDs supplied as targets; four selected context items, original requirement quotation included |
| Prompt to received request | Captured message arrays exactly equal stored/returned trace arrays; compact UTF-8 array digests reproduce stored SHA256 |
| Memory/history controls | Memory included; two previous turn messages included once; exclusion controls remove memory/history without deleting saved messages |
| Trace off | Successful request still reaches provider; returned/persisted trace absent |
| Provider HTTP 500 | One independently received request, native error, retained matching compilation trace |
| No API key | Native error, zero received requests, retained compilation trace: trace alone does not prove dispatch |
| Missing knowledge run | Native error, zero received requests, no compilation trace |
| Streaming | Actual request has `stream:true`; fixed SSE answer reconstructed with matching trace |
| Persistence | All six traces equal after process reopen and after reindex; reindex causes no provider requests |

The provider is a new standard-library Python server; it does not use
`ScriptedTransport`, production test helpers, production payload compilation, or
a language model. Raw request body bytes are preserved as base64 plus SHA256 in
the capture artifact. HTTP credentials/headers are deliberately not captured.
All data are new synthetic fixtures and the only key is a loopback dummy string.

## First-result caveat

Attempt 01's 68/68 checks did **not** establish nonempty knowledge delivery: the
initial request had no entity targets and returned an empty prompt. Manual
review identified this harness coverage defect. The original harness/results
remain intact in `transport_attempt_01/`; amendment 01 was written before attempt
02. Empty-target behavior is an existing documented limitation, not fixed or
hidden here. Attempt-01 `environment.base_sha` identifies the harness checkout,
not binary provenance; attempt 02 separates both source identities.

## Exact reproduction

Run from repository root with a previously unused output directory:

```bash
python3 loom/tools/w6_evidence/transport_audit.py \
  --server /workspace/scratch/a371a1ca13b1/verification/native-dev/server/loom-server \
  --library /workspace/scratch/a371a1ca13b1/verification/native-dev/libloom.so \
  --native-source /workspace/scratch/a371a1ca13b1/chatadhd \
  --output /workspace/scratch/a22a9f93cf94/w6-transport-attempt-02
```

Server SHA256:
`3fc10a60b7aa4e5336cf98e38a8b88d175d27b4ce0b351b4a43b823ff3262bd6`.
Library SHA256:
`0c11f60a09f91c808d104ab428c624258ed2aa81c3fe514d3160d47d027aae23`.
Harness SHA256 at run:
`2e56efc5ef2cc1f2f896fa5113a4593671cc30cc0cae931d90d7d0fbf4c312aa`.

`transport_attempt_02/` stores the source fixture, full non-secret API transcript,
provider capture, trace baselines, checks, environment, summary and native log.
Generated SQLite/runtime directories are scratch only; the experiment is
reproducible from the script and fixture, and no owner archive is involved.

## Limits

This establishes local native protocol/persistence behavior for the frozen
baseline, not provider comprehension, remote delivery, LLM answer quality,
automated entity resolution, current W1/W2 integration, whole-body trace hashes,
real-export fidelity, crash durability or inter-process concurrency. Stored
context hash covers compiled message serialization, not headers or complete
provider payload. The fake provider returns fixed strings and cannot validate
semantic use of any context. No new model-quality claim follows from this run.
