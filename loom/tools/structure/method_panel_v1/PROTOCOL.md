# Model-free Jev pair method panel v1 — frozen protocol

Frozen before computing any new method scores, 2026-09-30 UTC. Corpus:
`loom/tests/fixtures/eval/jev_structure_pairs_v1`, 48 authored pairs, 32 positive
and 16 negative labels. Inputs SHA-256
`3474eef3ee9060f790aae21ee30fd962569ae902f2352dda46deea576e44c545`;
gold SHA-256 `2db33aeba01fc004ace04bfa206e8d60d2aa171234822a7f36d71769f2a116c0`.
All four pair-fixture Git blob hashes and all four source-fixture blob hashes
match the remote-files index for current remote research head `3a3c6236`.
No holdout-key branch/file is read; no provider/model API is called.

All 48 pairs are **exploratory reused material**, including the inherited
`validation` half. The texts and existing question were partially inspected to
identify input contracts before this freeze. There is no independent validation
or test set here. No threshold, feature, weight, extractor policy or graph
projection will be tuned using these 48 labels/results. Preserve first scores,
errors and all denominators, then keep/reject/investigate a method without a
post-result repair to this arm. New hypotheses require a separately named arm.

## Target and availability

Gold means sameness of the authored **operation-and-role skeleton**, not lexical
similarity, identical propositions, entity identity, truth or full equivalence.
Cross-domain transfer ignores domain vocabulary and some finer topology. P/Q/T
local roles anchor corresponding propositions. The fixture has pair labels and
12-question source labels, but no independently annotated per-passage graphs,
logical formulas, grounded argument roles or alignments. Do not derive those
annotations from family names, variant labels or pair gold. Supplied-annotation
role/WL/alignment quality is therefore unavailable unless a genuinely separate
annotation input is discovered before scoring. Record absence, not zero quality.

Existing frozen raw-text extractors may be run as separate **source-derived,
unassessed candidate-envelope** channels. Their graphs preserve declared
operation/slot labels, not complete logical scope or local proposition identity.
Their partial coverage and opaque operands must accompany every score. Empty
graphs and exhausted alignment searches abstain; they are not negative matches.
A successful graph match does not certify correct source extraction.

## Fixed methods and representations

1. Always yes / always no: binary reference policies, no learned parameters.
2. `lexical_token_cosine`: casefolded Unicode `\w+` frequency cosine; existing
   weak baseline, no stemming/stopword removal.
3. `char_tfidf_cosine`: whitespace-normalized casefolded character 3–5-grams,
   sublinear term frequency `1+ln(count)`, IDF `1+ln((1+N)/(1+df))`, L2 cosine.
   This is **character TF-IDF, not a learned semantic embedding**.
4. `directed_word_path_cosine`: contiguous ordered Unicode-word bigrams with
   the same TF-IDF rule. This is a surface adjacency kernel, not dependency
   parsing or recovered argument structure.
5. `word_char_kernel`: equal-weight sum of the character and word-path cosines,
   equivalent to concatenated separately normalized feature blocks. Tests
   whether retaining ordered word adjacency changes failures; weights stay 0.5.
6. Frozen `bounded` and `explicit_relations` raw-text extractors, each followed
   independently by existing role/relation feature cosine, directed edge-labeled
   1-WL cosine (two rounds), and exact labeled-graph alignment (budget 10000).
   No formula/semantic output is manufactured. Unknown candidates remain unknown.
7. `nongating_union`: maximum of available lexical, character, word-path and
   source-derived role/WL/alignment scores. Equivalent to OR of fixed-threshold
   candidate sets. Any channel can add a candidate; no channel vetoes another.
   Scores are similarities, not calibrated probabilities. This is a recall
   policy, not automatic canonical-graph promotion.

IDF uses **unique exact source passage texts from all 48 input pairs**, without
labels, so repeated left passages are not overweighted. This is an exploratory
transductive corpus-frequency fit. No trained semantic embedding is represented
by it. Store unique-source counts and fitted-frequency hashes; retain frozen
raw extractor outputs/coverage and source hashes for reconstruction.

## Measurements, analyses and decisions

Predeclared thresholds: 0.25, **0.50 primary descriptive operating point**, 0.75,
0.90 (score >= threshold). They are generic similarities, not calibrated Jev
probability cutoffs. Do not select a best threshold after scoring. Retain every
score so future independent calibration can reuse the mechanism, not these labels.
For each method/threshold: TP, FP, TN, FN on represented pairs; abstained positive
and negative counts; precision TP/(TP+FP); planned recall TP/32; represented
recall TP/(TP+FN); represented and planned denominators. Unrepresented positives
are explicit missed opportunities, never silently removed from planned recall.
Also report tie-aware AUROC and tie-block average precision on available scores
with positive/negative denominators; ranking metrics omit unavailable scores and
must show that coverage limitation. Report per family, contrast, language and
inherited split (both exploratory). Preserve all false positives/negatives and
abstentions, diagnostic channel disagreements and score/rank gaps. No model
quality claims from mechanism tests.

Check source-byte hashes, fixture schema/counts, graph-label/source-output
version hashes and score determinism. Use synthetic mechanism tests for cosine,
IDF deduplication, missing-score denominators, tie metrics, union addition and
source-derived graph identity limitations. Run the existing structure regression
suite after independent mechanism tests; do not change existing gates/files.
