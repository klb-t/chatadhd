# STATE — ChatADHD / Loom (canonical, current)

Last verified: **2026-09-29** (build + full `ctest` run by Claude on the head
of `claude/chataddhd-cpp-loom-core-IRGRN`). This is the ONE document that
says where the project stands. Older status logs are history (see §9); if one
contradicts this file, this file wins. Update it at the end of every work
session — including test counts, with the date and commit.

> **Podsumowanie dla właściciela (PL).** Loom (rdzeń C++20) buduje się i przechodzi
> **71 z 72 testów**; jedyna czerwona bramka to jakość selektywnego katalogu
> (recall **13/45 = 29 %**, wymagane ≥ 55 %, cel projektowy 90 %). Cały
> sześcioetapowy potok wiedzy działa od początku do końca na korpusie
> syntetycznym i na tekstach repozytorium, ale **nigdy nie widział Twoich
> prawdziwych eksportów** — to największa luka. GPT dodał dużo badań (struktury
> myśli, Jev), naprawił kilka realnych błędów i uczciwie obniżył zawyżone wyniki.
> Do rozwiązania są: recall katalogu, kompletna interpretacja eksportów
> OpenAI/Anthropic, precyzja/wydajność rozwiązywania encji i uogólniania,
> uczciwy test predykcji w czasie, dalej UI i Android.

## 1. What this is

ChatADHD = Python/Kivy app (v0.7.10, `engine/ core/ gui/`, legacy, still
supported). **Loom** = its C++20 successor kernel in `loom/`: C ABI (`loom.h`),
CLI, `loom-server` (REST/SSE), React web workbench, Android shell (JNI), plus
a *knowledge layer* that turns sources (conversations, documents, code, git
history) into assessed claims → principles/operators → predictions → context
sets and products. Owner's philosophy and requirements: `docs/architecture/`.

## 2. Repository map

| Ref | Meaning |
|---|---|
| `claude/chataddhd-cpp-loom-core-IRGRN` | **Development line** (assigned branch). = Codex head `8164067` + Claude docs on top. |
| `codex/loom-handoff-2026-09-28` | GPT/Codex line; **draft PR #6 → `main`** (180 commits, 815 files). Fully contained in the line above. |
| `main` | Old Python-only 0.7.10 line. Merging PR #6 is the owner's decision. |
| `eval/real-holdout-key` | **Secret answer key** for the temporal-holdout benchmark. Never read or tune against it during development. |
| `wip/*` | Safety snapshots of interrupted agent work (content already merged). |

The GitHub repo is **private**. Actions minutes are exhausted until
**2026-10-01**: end commit messages with `[skip ci]`; verify locally.

## 3. Read in this order

1. `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` — R1–R24, owner's words.
2. `docs/architecture/LOOM_CONCEPTUAL_MODEL.md` — the binding vocabulary/invariants.
3. `docs/architecture/NOTATKA_GPT_2026-09-26.md`, `MEGA_MASTER_2026-09-16.md`.
4. This file, then `docs/research/PROGRAMME_2026-09-28.md` + `RESULTS_2026-09-28.md`
   (thought-structure research), `docs/CATALOG_QUALITY_2026-09-28.md`,
   `docs/research/CATALOG_SEMANTIC_GAP_2026-09-28.md`, `docs/selfhost/v2/README.md`.
5. `CLAUDE.md` (conventions, Python↔C++ compat invariant), `loom/README.md`.

## 4. Verified today (2026-09-29, head `8164067`+docs)

| Check | Result |
|---|---|
| `cmake --preset dev` + full build (112 targets) | OK |
| `ctest --preset dev` | **71/72**; sole failure `unit.test_catalog_eval` (recall gate) |
| Catalog eval on `synthetic_dev` | recall **13/45 = 0.2889** (gate ≥ 0.55), labeled-conversation precision 13/13, traps 0/5, generic noise 0/15 |
| Test count consistency | 60 / 63 / 72 registrations match the 59/60, 62/63, 71/72 claims in the older docs |
| Quality gates weakened? | No: thresholds 0.55 / 0.75 / 0.05 unchanged; the failing gate stays red |
| Secrets in Git | Pattern scan of the whole Codex branch found none (API keys, tokens, private keys) |
| Actions/model switches | Both OpenRouter request switches disabled; no automatic spending |

Not re-run today: ASan/TSan presets, Chromium e2e (16/16 claimed), Python
research suites (387/387 claimed), host JNI smoke (claimed passing).

## 5. Measured quality (honest numbers; always read the caveats)

- **Catalog (selective import)** — 13/45 recall. The earlier 0.62–0.67 was
  **inflated** by substring alias matching (`EP`, `LEM` inside ordinary words);
  strict word boundaries exposed the real value. Diagnosis: all 32 misses are
  scored but stay in the *irrelevant* band; evidence is purely lexical
  (`sigmoid(-3 + 1.4·bm25_self + bm25_phil + link)`); `ScoreConfig.llm` and
  `verify_max_units` are parsed but **never used**. Details: `CATALOG_SEMANTIC_GAP`.
- **Knowledge pipeline** (`docs/selfhost/v2/README.md`), synthetic dev corpus:
  selective vs full import — decision recall 9.5 % vs 57 %, principle recall
  31 % vs 85 %, operator recall 0 % vs 17 %, alias recall 24 %, fork recall
  0 % vs 100 %. Structural evidence violations 0 (structural check, not truth).
  Numbers were measured at pipeline 4 / scanner 3; head is newer — re-measure.
- **Repository run** (283 files, older binary): 355 s, of which `generalize`
  264 s; 504 products; visible noise: code fragments (`const Json& d`) as
  projects, a software project matched to a *music* paradigm, wrong computed
  version.
- **Thought-structure research** (`loom/tools/structure`, offline prototypes):
  works on *supplied* graphs (WL/alignment 11/11 orderings), fails on raw text
  (0 structures recognised in three real repo documents; parser catches 36/51
  formulas on the broad set). Premise-binding composition 2/6 → 6/6 on 21
  fresh scoped cases (simple syntax only). Nothing here is production.
- **Jev** (TypeSafe AI `jev-1.13`, via OpenRouter; typed yes/no judgments):
  64 requests/768 decisions for USD 0.0053; accuracy 96.6 % but positive
  precision 71.7 %, complete 12-bit vectors 39/64; "always no" scores 91.4 %.
  Follow-up 48 pair questions 47/48 (exploratory, reused texts). Useful as a
  cheap *judge of supplied hypotheses*, not as an extractor.
- **Temporal holdout**: strict predictive accuracy is **unavailable** — the
  data pack contains later knowledge (contaminated); runs with priors cut are
  only "retrospective consistency". Needs a neutral pack (`priors=none`) and
  post-cutoff scoring.

## 6. Bugs found and fixed (Codex work, mirrored in Python and C++)

- `Config.auto_upgrade` reset any `semantic_model` containing `claude-haiku-4`
  (incl. the recommended `anthropic/claude-haiku-4-5`) to `""` → **the owner's
  cheap semantic model was silently disabled** in Python 0.7.10. Fixed in both
  languages; compat test updated.
- `SemanticLLM` (Python) made thread-safe with config-identity tracking.
- Alias substring matching; self-reinforcing project attribution; full import
  extracting only selected units; catalog raw-source retention; JSON locators
  not matching hashes; Android callbacks on early errors; a research test that
  called the live GitHub API.

## 7. Open work, by priority

1. **Real exports** (blocked on owner data): everything is measured on
   fictional/synthetic material. Meanwhile make the OpenAI/Anthropic export
   handling *complete and lossless* from public format knowledge (R21).
2. **Catalog recall**: semantic evidence beyond lexical (morphology-aware
   whole-token aliases, profile-vector cosine incl. shared foundations,
   optional embeddings, coverage-first LLM triage); rank and select measured
   separately; validate on independent data.
3. **Precision/perf of resolve+generalize** on repository-scale input.
4. **Honest temporal benchmark** (neutral pack, post-cutoff scoring).
5. Native use of the cheap semantic model and Jev-style judges for structure
   (usage rules in `JEV_USAGE_RULES`), all behind explicit budgets and caches.
6. UI: multiple coordinated simultaneous views, layout persistence/free
   docking, judgement editing, provider-inspired interface profiles (R16/R17/R21).
7. Android device validation; privacy/threat-model layer (R19); legal-case
   kind (R9).

## 8. Needed from the owner (only when convenient)

- Real ChatGPT and Claude export ZIPs (or a slice) — the one thing no amount
  of synthetic work replaces. The ChatGPT *data export* (no scrolling) also
  contains conversations like the GPT session, unlike a saved page (`.mht`
  keeps only the ~9 rendered turns of a virtualized list).
- An OpenRouter key with a small cap (for live semantic/Jev runs), delivered
  outside Git (environment secret), if live measurements are wanted.
- Nothing else is blocking; the docs above are sufficient to continue.

## 9. History (superseded status logs — keep, do not extend)

`AGENTS.md` (old accreted log → `docs/research/AGENTS_STATUS_LOG_2026-09-28.md`),
`docs/WORK_STATUS_2026-09-28.md`, `docs/CODEX_HANDOFF_2026-09-28.md`,
`docs/HANDOFF_2026-09-28.md` (Claude → GPT), `docs/WORK_RECOVERY_2026-09-28.md`.
