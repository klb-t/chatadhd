# Model-method panel continuation — 2026-09-30

Frozen before new provider inference. Continue the existing 48-pair same-task
GPT-4.1-mini comparator without changing inputs, question, prompt or scoring.
The original protocol is `STRUCTURE_PAIR_COMPARISON_PROTOCOL_2026-09-28.md`.
The new endpoint snapshot updates pricing evidence only; first responses remain
immutable and unsuccessful/uncertain attempts are never paid retries.

Research scope was corrected by the owner on 2026-09-30: investigate vectors,
cosine, algorithms, Jev and LLM judges in parallel, with multiple query recipes.
The existing dedicated OpenRouter key authorization is one nonresetting USD 2
limit, documented in `OPENROUTER_ACTIONS_SETUP_2026-09-28.md`; no fresh USD 2
budget is created by this continuation. The owner supplied a fresh encrypted
credential handoff in this session. Its plaintext stays outside Git and artifacts.

The runner must check the actual key limit, balance and billing mode before POST.
The refreshed public snapshot records GPT-4.1-mini provider `openai`, status 0,
USD 0.40/M input and USD 1.60/M output. Provider fallbacks are disabled and the
request max-price guard remains unchanged. The 48-request reservation is
USD 0.0912696 under the USD 0.10 batch ceiling; it is conservative accounting,
not a formal maximum price guarantee. Actual costs and missing cost records
must be reported. No GitHub Actions are started.

An independent Jev recipe pilot reserves at most USD 0.032 for 32 first calls,
and the prepared context arm would reserve at most USD 0.048. The three finite
arms together reserve USD 0.1712696, subject to actual remaining key balance.
They are separate task profiles; their scores must never be pooled into a
single global model-reliability number. The graph-method corpus is a separate
source-to-graph/edge-judgment experiment and is not validated by this pair task.

Status at freeze: GPT first calls 0/48; Jev recipe first calls 0/32; context
first calls 0/48. Local learned-embedding and model-free scores are measured
separately and preserved before comparison. Existing Jev 48-pair results are
historical first responses, not rerun calls. All 48 pairs are reused authored
exploratory material; no threshold tuning or holdout claim is permitted.

Fresh prepared comparator manifest: canonical SHA-256
`517a6f67b0e1d21bdcc578aa64b2ef65fa5aa7ccf75e35e9856b5748ad45b6e3`.
Inputs byte SHA-256:
`3474eef3ee9060f790aae21ee30fd962569ae902f2352dda46deea576e44c545`.
Public endpoint evidence retrieved `2026-09-30T01:49:34.956981+00:00`.

Keep all denominator counts, precision and recall separately, errors, missing
outputs, costs, latency, first request/response hashes and per-case disagreements.
Do not promote a successful classification into graph truth or merge identities.
