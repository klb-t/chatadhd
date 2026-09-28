# Native source-anchor assistance: offline post-hoc experiment

## Protocol frozen before replay

This method was designed **after** observing contract failures in the native
development responses. It is a post-hoc diagnostic arm, not a preregistered
holdout result and not a new model run. Original responses and scores remain
immutable. The transformation is `unique_exact_quote_utf8_v1`.

Rules fixed before applying the method to the saved responses:

1. Bind each response to its original manifest request, validated ledger hash,
   exact raw-response SHA-256, frozen case and source-packet hash. Use only the
   first alternative, as in the existing scoring protocol.
2. Strict JSON parsing rejects repeated keys. Never repair duplicate keys,
   schema fields, local identifiers, claims, roles, symbols or quoted strings.
3. Visit only existing source-support records in `entity_drafts`,
   `claim_drafts[].assessment.basis`, `coverage` and `unknowns`. A support span
   must already have exactly `observation`, `quote`, `byte_start` and `byte_len`;
   the two coordinate fields must be integers, not booleans.
4. The observation must exist in the exact source packet. The quote must be a
   nonempty string with exactly one literal occurrence in that observation.
   Count overlapping occurrences; do not select a repeated occurrence even if
   the old offset appears to identify one. No case folding, whitespace changes,
   Unicode normalization, tokenization, stemming or synonym matching.
5. For a unique match only, derive UTF-8 byte start and byte length from the
   unchanged source text and quote. Preserve all other fields. Correct spans
   stay unchanged. Ambiguous, missing, malformed and foreign spans stay
   unchanged with an audit reason; downstream validation still decides admission.
6. Keep an audit of old/new spans, packet and bundle hashes and source response
   hashes. The output is a derived candidate, never an overwrite or promotion.
7. Apply the same frozen candidate validator and development scorer before and
   after. Report contract admission separately from structural/semantic scores.
   Keep all original 32 planned requests visible, including duplicate-JSON,
   uncertain and untouched outcomes. No model call, semantic judge or graph write.

The aim is to measure whether moving mechanical byte bookkeeping out of the
model helps expose the quality of the structure it already proposed. Literal
matching here locates the model's own quote; it does not classify meaning.

## Results

The offline replay completed on the 20 returned native-model responses from
the two disjoint runs documented in
[OPENROUTER_NATIVE_REMAINDER_2026-09-28.md](OPENROUTER_NATIVE_REMAINDER_2026-09-28.md).
The original denominator remains 32 requests: 20 returned, two uncertain, and
ten untouched. No additional request was sent.

| Measurement | Original candidates | Assisted candidates |
|---|---:|---:|
| Strictly parsed candidates eligible for this arm | 14 | 14 |
| Full source-support check passes | 0 | **12** |
| Full graph contract passes | 0 | **0** |
| Structural exact score | 0 | 0 |
| Anchored semantic exact score | 0 | 0 |
| Semantic exact score | 0 | 0 |

Source-support success is 12/14 among the parsable candidates, 12/20 among all
returned responses, and 12/32 over the original plan. These are different
denominators. No graph passed the full contract, so conditional semantic
accuracy among admitted graphs remains unmeasured. A correctly located quote
does not establish that its attached claim correctly interprets that quote.

The method changed **92** span coordinates, left **26** already exact spans
unchanged, and preserved **five** malformed spans unchanged. Those five spans
were missing their `quote` field; the adapter did not invent it. No saved span
in this replay exercised the absent/ambiguous-quote branches; those branches
are covered by the unit tests, including overlapping matches.

| Model | Parsable candidates | Full source support after assistance | Full graph contract after assistance |
|---|---:|---:|---:|
| GPT-4.1 mini | 5 | 4 | 0 |
| Qwen3 30B A3B Instruct 2507 | 9 | 8 | 0 |

The six GPT responses with duplicate JSON keys remain `invalid_json_unchanged`.
They were not parsed with a last-value-wins policy, merged, or scored using a
selected repaired reading. Two uncertain outcomes and ten untouched requests
are preserved as such in the machine report.

Once coordinate errors are removed, the first graph-contract failures are:

| Failure | Count | What remains unsupported by this adapter |
|---|---:|---|
| `predicate` | 4 | Structural predicate outside the permitted graph vocabulary |
| `shape` | 4 | Scope attributes do not have the required fields |
| `literal` | 2 | Operation/type claims do not use the required literal representation |
| `handle` | 2 | Invalid local occurrence identifier |
| `subject` | 1 | Claim subject is not a local occurrence |
| `scope_membership` | 1 | Missing exact local-scope membership |

These are the first failures returned by the unchanged validator, not all
defects present in a response. They explain why improved grounding alone did
not yield an admissible graph. This does not establish the correctness or
incorrectness of the intended semantic structures behind invalid encodings.

## Reproducible record

Implementation: `loom/tools/structure/anchor_assist.py`.
Tests: `loom/tools/structure/test_anchor_assist.py` — **7/7 pass**. Tests cover
UTF-8 offsets, immutability and idempotence, overlapping/repeated quotes,
case/Unicode/whitespace non-normalization, malformed and foreign spans,
declared support locations only, source identity, and duplicate JSON refusal.

Before replay, the rules and implementation hashes were recorded as:

- Rules SHA-256: `bfbfce8fd95a455d84511594a71eeecf6580c25c42dab256623d19fe1841a877`.
- Implementation SHA-256: `c8b1f5ae8dd11bf232e68d3bbea20f3bffc6c26667d23b30656e36046276857a`.

The complete [machine report](inputs/native-anchor-assist-report.json) retains
all 32 request IDs, original raw-response hashes, manifest/ledger bindings,
unchanged scoring-code hashes, paired scores, original/derived bundle hashes,
derived bundles and every old/new span. Its file SHA-256 is
`89e24453d6f17a773a3ab3aba8e11f3bb59be0054966c255b3e89f02c4bdb961`.
The report contains 193,231 bytes. It was written once; rerunning must use a
different output path. The original archive and response files remain unchanged.

To replay locally against unpacked originals:

```sh
python -m loom.tools.structure.anchor_assist \
  --run-dir /path/to/live-structure-dev-v2 \
  --run-dir /path/to/native-remainder-v1 \
  --output /path/to/new-anchor-assist-report.json
```

The adapter verifies exact request/source binding, frozen development inputs
and gold hashes, scorer/validator/vocabulary hashes, ledger prefixes and
response hashes before evaluation. It rejects overlapping attempts across
runs. It reads development gold only for scoring, never for selecting or
changing coordinates. Validation gold and private holdout data are not read.

## Consequence for the next experiment

The measured improvement supports treating UTF-8 coordinates as deterministic
bookkeeping supplied outside the model. It does **not** justify admitting these
graphs, relaxing graph validation, changing word-based classification rules, or
claiming synonym invariance. No semantic operation, role, scope, entity, symbol,
claim or source quote was changed by this method.

A separate prospective arm can ask the semantic model for a simpler explicit
structural proposal and compile it into the existing graph contract with
deterministic identifier allocation and schema construction. The compiler
must preserve ambiguity and refuse missing semantic decisions; it must not
infer a conditional, role or class from keywords. Compare that arm on frozen
paraphrase/synonym pairs and meaning-changing role/scope/negation pairs. This
post-hoc coordinate result supplies a concrete reason to test that division of
work; it is not a completed test of JEV or a replacement for that priority.
