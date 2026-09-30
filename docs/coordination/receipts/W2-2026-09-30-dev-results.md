# W2 native retrieval — synthetic DEV v1 results

The native graph+TF-IDF union recovered more labelled evidence in this fixture,
but it also admitted noise: the misleading wallpaper claim ranked ahead of the
checkpoint evidence. This is a mechanism diagnostic, not semantic-quality or
generalization validation. No generated answers were evaluated.

## Instrument and retained first execution

- [Protocol](W2-2026-09-30-dev-protocol.md) was committed before execution and
  published as `4ade73fb5d941505d8009c774e693448218a4bcf`. Its local pre-publication
  commit was `f02aa0b9a9e25e7acd6ad729b9aa998bf1b76e08`; connector publication
  preserved the bytes/tree while replacing commit metadata.
- **7 distinct synthetic claims; 6 scenarios; 8 query–gold incidences**, not
  eight distinct claims. Five scenarios have nonempty gold; the one-token case
  repeats the main checkpoint question. Four configurations yield 24 result
  rows. These conditions are deliberately constructed, not a representative
  production sample or independent/held-out corpus.
- Test source: `loom/tests/test_context_retrieval_dev.cpp`, SHA-256
  `1a7d5cf2fe4f23eb33fd4536933149674d0d3f7edb97836c7977fb980e7f29e6`.
  Pack hash: `b149a86f452d2594b80088da6fe0d990b1a766d72a7b841c1b86f41970251759`.
- [First raw log](../../research/w2_retrieval_2026-09-30/verification/dev-first.log):
  exit 1; one test case failed, **239/241 assertions passed**. Both failures were
  strict graph/shadow item equality: `direct_goal_candidate` metadata was added
  only on the diagnostic route. Retrieval IDs, rankings and measurements were
  already identical. The first log is retained unchanged; SHA-256
  `5f2fd4106dc29315f5bc38df388b4b5f0d6abfd0f9d5e6de82609f85ad7d0de0`.
- The implementation repaired consistent direct-route tagging; **no fixture,
  gold, threshold or test was changed**. Corrected implementation, published as
  `9a6f54c254b3996946bc87067b467d6c927b8361` (tested tree `cd89eae`), passes the
  unchanged DEV case: **241/241 assertions**. The
  [corrected targeted run](../../research/w2_retrieval_2026-09-30/verification/targeted-corrected.log)
  passes 20/20 CTest entries; these are suite entries, not 20 DEV cases.
- Independently parsed the two raw logs: all 24 request/gold/token-use/rank/metric
  rows and all four metric summaries are exactly equal between first and corrected
  runs. Machine-readable data:
  [first](../../research/w2_retrieval_2026-09-30/verification/dev-first-data.json),
  [corrected](../../research/w2_retrieval_2026-09-30/verification/targeted-corrected-data.json).

## Measurements

Candidate recall uses selected plus budget-dropped claim IDs in this
dependency-free fixture, not every scanned document. Precision uses emitted
claims. MRR is over the five nonempty-gold scenarios, including the repeated
low-budget scenario. Candidate rank is final selector score descending then ID;
selected rank is emitted order. It is not the raw cosine ranking.

| Configuration | Candidate recall | Selected recall | Selected precision | Candidate MRR | Selected MRR |
|---|---:|---:|---:|---:|---:|
| graph | 2/8 = 25% | 1/8 = 12.5% | 1/2 = 50% | 0.40 | 0.20 |
| tfidf | 5/8 = 62.5% | 4/8 = 50% | 4/6 = 66.7% | 0.70 | 0.60 |
| union | 7/8 = 87.5% | 5/8 = 62.5% | 5/8 = 62.5% | 0.70 | 0.60 |
| graph_shadow | 2/8 = 25% | 1/8 = 12.5% | 1/2 = 50% | 0.40 | 0.20 |

| Configuration | Candidate hits@1 / @3 | Selected hits@1 / @3 |
|---|---:|---:|
| graph | 2/5 / 2/5 | 1/5 / 1/5 |
| tfidf | 2/5 / 5/5 | 2/5 / 4/5 |
| union | 2/5 / 5/5 | 2/5 / 4/5 |
| graph_shadow | 2/5 / 2/5 | 1/5 / 1/5 |

The no-answer case emits zero candidates and zero selected claims in all four
configurations. Its recall/RR/hits and empty-selection precision are `null`,
not fabricated perfect scores; it is excluded from MRR/hits denominators.

## What these cases show

- In the main checkpoint case, graph finds the connected durable-resume evidence;
  TF-IDF finds disconnected offset evidence present only in its support quote.
  Their union retrieves/selects both gold claims, plus toolbar and wallpaper
  noise: 2/2 evidence recall and 2/4 selected precision. The wallpaper ranks first,
  offset second and resume third. Greater coverage did not remove the ranking
  error. The short literal query is intentionally ambiguous; fuller task-aware
  relevance judgment is not measured here.
- The support-only orchid and PL `szyfrowanie` → EN `Encryption` cases each
  retrieve/select 1/1 gold with TF-IDF and union, while graph has no reachable
  evidence. This demonstrates support-text indexing and the existing glossary,
  **not neural semantics or general multilingual understanding**.
- With the empty graph anchor, the misleading-word case retrieves offset plus
  wallpaper through TF-IDF, misses resume, and still ranks wallpaper first.
- The one-token case retains candidate recall (graph 1/2, TF-IDF 1/2, union 2/2),
  but selects no claims. It separates retrieval coverage from budget loss.
- Lexical shadow changes no selected IDs, scores, item metadata or budget drops
  after the tagging repair. It exposes offset/orchid omissions and also the
  irrelevant wallpaper as an **undecided diagnostic gap**. It does not find the
  PL/EN glossary match or keyword-free resume evidence. A lexical omission flag
  is therefore neither a relevance label nor a veto on another channel.

No quality threshold was added or tuned from these outcomes. There were no model
calls, paid inference, answer-correctness measurements or sealed validation reads.
The remaining retrieval-quality question requires independently labelled real
queries and evidence; this tiny synthetic comparison does not settle it.
