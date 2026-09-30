# Jev context scorer

`loom/tools/structure/jev_context_score.py` implements the evaluation frozen in
`JEV_CONTEXT_PROTOCOL_2026-09-28.md`. It is offline and reads development labels
only. There is no model-quality result in this document.

```sh
python -B loom/tools/structure/jev_context_score.py \
  --manifest /ABS/live-prepared/manifest.json \
  --run-dir /ABS/first-responses \
  --prepared /ABS/jev-context-dev-prepared \
  --output /ABS/new-context-score.json

python -B -m unittest discover -s loom/tools/structure \
  -p 'test_jev_context_score.py' -v
```

The scorer first verifies the offline bundle hashes, current frozen adapter/runner
code, live manifest and all 48 request mappings. It regenerates each projection
from the saved unmodified causal export: changing a question-to-ID map and updating
its hash cannot silently change the task. It checks the frozen input, split,
protocol and materializer hashes before reading labels. The development ID set is
also checked before parsing any label body. Non-development JSONL rows are skipped
using their leading case ID; their label bodies are not decoded or parsed.
Before label parsing, the whole `gold.jsonl` file is hashed as opaque bytes against
the pinned existing corpus digest
`2682a8e23346ae32ba34d69a74b7c350b89aceb743226c42e76186ecc4cdabf5`.
This binding does not trust a mutable current validation manifest and does not
interpret validation labels. A changed gold file aborts before label parsing.

The live ledger must match the manifest and original response SHA-256 values.
Any integrity mismatch aborts scoring; it is not silently reported as a model
error. Completed responses are parsed through the existing Jev response validator.
One malformed or missing answer invalidates that whole response, while the target
stays in all-query denominators. Rejected, uncertain, started and unattempted
requests earn no semantic credit. The tool never requests a retry or repairs an
answer.

The report separates membership bits from Claim-selection bits and contains:

- TP/FP/TN/FN, precision/recall/F1, negative prevalence and always-false accuracy;
  unavailable positives count as FN, while unavailable negatives never count as
  correct TN. Conditional valid-output counts are separate.
- Topic-set, Claim-set and joint exact-set accuracy on all 48 messages. Even a
  true empty set earns exact-set credit only for a valid complete response.
  Claim-set accuracy is also reported only for messages with candidates.
- Coverage, valid-output Brier score, fixed-threshold decisions at 0.5 and the
  predeclared selective view at <=0.2 or >=0.8, with retained coverage/errors.
- Language and family breakdowns, request-level expected/predicted/extra/missing
  IDs, and individual probabilities, labels and error types. The request-local
  question name is never treated as a globally fixed phenomenon.

Undefined ratios and F1 remain null. No-candidate messages do not create negative
Claim bits. The report records source, manifest, ledger, development-label and
scorer hashes, plus the ledger's reported cost. These hashes bind artifacts; they
do not independently authenticate a provider or validate a billed amount.

**10 tests pass.** Fabricated probabilities verify threshold boundaries, separate
task metrics, missing-negative handling, empty-set validity, candidate-subset
denominators and precise errors. Integration tests exercise response tampering,
map tampering, invalid complete responses and full development-gold oracle wiring.
The oracle's 48/48 exact vectors verify joining and scoring only: the responses
were constructed from labels, with zero model calls. A deliberately malformed
validation label body verifies that the development reader skips it unparsed.
Gold-byte tampering is separately rejected before the label reader can run.

Frozen scorer SHA-256 before live inference:
`41975d03937a9f15027eadab3f3ecb32a091a1cb4bdf3b52741963466c41c714`.
The projection and prompt remain unchanged. This scorer does not measure free
topic discovery, span/boundary extraction, graph updates or archive retrieval
recall outside the supplied candidate universe.
