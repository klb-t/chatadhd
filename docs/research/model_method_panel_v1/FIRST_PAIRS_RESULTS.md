# First live GPT comparator and local representation results

The first GPT-4.1-mini series completed all 48 frozen structure-pair requests:
32 TP, 16 TN, zero FP/FN, precision 32/32 and recall 32/32. Both language
subsets contain 24 completed correct decisions. Reported cost is USD 0.0111276,
against a conservative USD 0.0912696 reservation; no paid retry was made.
`gpt41mini_same_pairs/` retains the exact manifest, endpoint evidence, first
responses, ledger and score. The request and original scoring protocol were
unchanged. The mechanism comparator tests passed 6/6 independently.

These are previously authored and inspected exploratory pairs, including an
inherited subset named validation. None is a blind holdout. Classification of
two supplied passages does not measure source-only graph construction or
archive retrieval recall. Keep this comparator as a diagnostic reference;
investigate source attribution, temporal corrections and evidence extraction
on the newly frozen development conversations instead of repeating these calls.

The local alternatives are independently preserved under
`loom/tools/structure/method_panel_v1/` and `local_embedding_panel_v1/`.
At the fixed primary threshold 0.5:

| Instrument / representation | TP | FP | FN | Precision | Recall |
|---|---:|---:|---:|---:|---:|
| Token-frequency cosine | 15 | 16 | 17 | 15/31 | 15/32 |
| Character 3–5 TF-IDF cosine | 2 | 14 | 30 | 2/16 | 2/32 |
| Non-veto max union of frozen channels | 17 | 16 | 15 | 17/33 | 17/32 |
| Learned multilingual MiniLM cosine | 22 | 16 | 10 | 22/38 | 22/32 |
| GPT-4.1-mini, original structured judgment | 32 | 0 | 0 | 32/32 | 32/32 |

The learned encoder recovered all 16 paraphrases and six of sixteen domain
transfers, but also predicted all sixteen shared-vocabulary structural foils
positive. Its AUROC is 5/512, not evidence that cosine reliably captures the
directed operation skeleton. Fixed-threshold results are diagnostics, not a
selected optimum. Continuous similarities remain scores, not probabilities.

Raw graph extraction has a separate coverage limitation: the bounded parser
represented 0/48 passages and the exploratory envelope 12/48. Exact alignment
on the latter gives precision 7/9, planned recall 7/32, represented-positive
recall 7/8. Neither conditional alignment nor a stable graph signature proves
semantic correctness when source parsing is incomplete. No independent graph
annotations exist for these older pairs (0/48), and the newer panel supplies
that missing evaluation contract.

All original scores and vectors are retained. The local panel's first errors,
subsequent environment correction and independent recomputation are archived
beside its protocol. The 436-test current-remote baseline and 10 new panel
mechanism tests passed; root subsequently ran 491/491 structure tests and
153/153 contract tests, with no quality gate changed.
