# Handoff → GPT‑6.1 Sol (agent mode, OpenRouter access) — 2026-09-29, late evening

From Claude. You have what we lacked: **an agent runtime with a live OpenRouter key.**
The focus is what we have been grinding on for days: **the knowledge graph and the
selector** (what goes into the graph, how relevance and context are chosen). Test the
approaches discussed below with real models, invent better ones, iterate, optimise —
measured, reproducible, within budget. Directions, not orders; the owner explicitly
wants you to explore.

## 0. Start here (15 min)

1. Branch: **`claude/chataddhd-cpp-loom-core-IRGRN`** (the development line; private repo
   `klb-t/chatadhd`). Start from its current tip. Do not force-push it.
2. Read: `docs/STATE.md` (canonical state, verified numbers), `AGENTS.md` (rules),
   `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` (R1–R38, D1–D2 — owner's words),
   `docs/architecture/LOOM_CONCEPTUAL_MODEL.md` (one vocabulary; §11 = v1.1),
   `docs/architecture/ACCEPTANCE_TESTS_2026-09-29.md`, `CLAUDE.md` (build/test).
3. Build + test (Linux, CMake ≥3.28, Ninja, GCC/Clang):
   `cd loom && cmake --preset dev && cmake --build --preset dev -j && ctest --preset dev`
   → expected all green after the round‑3 catalog merge (before it: 73/74 with only the recall gate red).
   Python research/eval suites: see §5.
4. GitHub Actions minutes: used up until **2026-10-01** → commit with `[skip ci]`, verify locally.

## 1. Where things stand (verified today unless marked)

| Area | State |
|---|---|
| Loom core (C++20) | Full port of the Python app + provenance, tasks, C ABI, CLI, server, React workbench, Android shell. Python compatibility is **no longer required** (owner D1); compat tests stay as sentinels. |
| Knowledge pipeline | catalog → extract → resolve → assess → generalize → materialize, resumable, deterministic. **Repository-scale runs now byte-identical** (use-after-free in extract date matching fixed today). |
| Performance | generalize ~6× faster (repo run 1064 s → 177 s at -O0); extract `extract_units` (~79 s) is now the biggest cost. |
| Export import | Lossless on 6 synthetic OpenAI/Anthropic exports (1604/1604 JSON leaves); `loom import <zip> --audit`. **Never run on real owner exports.** |
| Catalog (selective import) | synthetic_dev: recall **31/45 = 0.689** (gate 0.55 now passes; ctest all green on its branch), precision 31/31; ranking AUC 0.966; 14 relevant units still missed; weights of the new `consensus` feature were set on dev data → first real check = blind corpus, traps 0/5, generic noise 0/15. Channels: lexical (BM25/aliases/links) + TF‑IDF cosine (word stems + char 4‑grams). `ScoreConfig.llm`/`verify_max_units` exist but **no model is called**. Pair linking now uses candidate generation (LSH/inverted index), output‑identical to exhaustive. |
| Precision | Known noise: code fragments (`const Json& d`) become projects; a software project matched to the *music* paradigm; wrong computed versions; 8,928 claims / 506 principles for 283 files. |
| Context engine | Three bands (stable → project → goal), `why` per item, budget, dependency closure (a premise that cannot fit is now flagged `missing_premises` / `[INCOMPLETE]`). **Not wired into chat** (`ChatEngine` never uses it). |
| Thought-structure research (`loom/tools/structure`, Python) | Works on *supplied* graphs; fails on raw text (0 structures in 3 real repo docs). Premise-binding composition 2/6 → 6/6 on a small scoped set. |
| Jev (TypeSafe, via OpenRouter) | 64 requests / 768 decisions: accuracy 96.6 % but positive precision 71.7 %; per-question profiles differ strongly (q01 precision 30 %). A cheap *judge of supplied hypotheses*, not an extractor. |
| Offline assets from GPT (you, earlier) | T1 contracts (5 layers), T2 refinement corpus+evaluator, T3 Jev recipe experiments (36 cases, **208 bodies / max 184 calls, never run**), T4 ModelProfile (14 profiles), T5 workspace UI‑IR — all merged, **297/297 Python tests**. |

### In flight right now (Claude agents; branches will appear on the remote)
- `wip/precision` — extract/resolve/generalize precision (items in "Precision" above) and the
  `User` claim `confidence = 1.0` split (reading fidelity vs credibility vs authority).
- `wip/catalog-r3` — catalog recall toward the gate, lexical-shadow diagnostic, LSH/inverted-index
  candidate generation instead of O(N²).
**Coordination:** check whether these branches exist and what they report
(`docs/research/PRECISION_ROUND1_2026-09-29.md`, `CATALOG_RECALL_ROUND3_2026-09-29.md`).
If they finished, merge them first (ratchet rule below); if they are unfinished, either build on
them or work in other files — do not edit the same files in parallel and then overwrite.


### Update (container restart) — status of the in-flight branches
- `wip/catalog-r3` — **merged** (recall 31/45, gate passes).
- `wip/precision` — **interrupted, NOT verified** (container restart). 3 commits on top of the
  T5 merge, 17 files (+761/−118): name-plausibility rules as data (code fragments must not
  become projects), distinct-weight paradigm cues (against cross-domain matches such as
  software→music), `User`-class reading fidelity separated from authority, a prose gate and
  third-party version binding. Last commit is an untested WIP snapshot. To use it: merge onto
  the current tip, build, `ctest --preset dev` (must stay all green), run
  `knowledge_eval.py synthetic` + `selfhost` before/after and keep only changes with measured
  non-negative effect; write `docs/research/PRECISION_ROUND1_2026-09-29.md`.

## 2. The problem you are asked to attack

**What the owner wants from the graph and the selector** (R3, R12, R22–R25, R27–R28, R32–R37):
- The graph holds *things and structures of thought* (projects, components, decisions,
  principles, arguments), not word soup; everything the app uses lives in it with rich,
  time‑scoped, provenance‑carrying edges; `same_as` only for real identity; lineage /
  convergence / composition / sharing as relations.
- The selector finds **everything relevant by every method** — vectors, a cheap semantic
  model, a classifier (Jev), graph structure — while keywords/regex are a *complementary*
  channel and a **shadow pipeline** that evaluates the semantic one
  (`diagnostic_gap = C_lexical − C_semantic`; verified rescues become regression cases).
- Context selection is goal‑directed with **scope** and **detail** as independent controls;
  a product plan can act as a structural query; the stable → volatile ordering also lets
  prompt caches help.
- The generator: principles, operators and a **model of models** (per operation/domain
  instrument profiles) — measured by **temporal hold‑out**, never by eloquence.

## 3. Concrete experiments you can run now (live), in suggested order

> **Owner, 2026-09-30: paid calls are allowed, but funds are limited — use them wisely.** Do what
> can be done offline offline (scripted transport, fixtures, saved responses). Spend where a live
> result actually informs a decision, with the smallest sample that answers the question. Saving
> most of the money for the owner's real archives beats spending it on synthetic data, where the
> results transfer weakly. Record the cost of every live run.

Each with a frozen plan, a budget cap, saved first responses, no silent retries, and results
split into development vs untouched validation. The OpenRouter key must never enter Git.

1. **Semantic evidence for the catalog** (biggest measured gap). All 24 still‑missed units on
   synthetic_dev land in the *irrelevant* band with lexical evidence only
   (`LOOM_CATALOG_EVAL_VERBOSE=1 loom/build/dev/loom_tests --test-suite=catalog_eval`).
   Implement the promised seam **before selection** (`docs/research/CATALOG_SEMANTIC_GAP_2026-09-28.md`
   "Generic integration seam"): coverage‑first windows from **all** score bands; the cheap
   `semantic_model` proposes topic/project membership with exact quotes; optionally Jev judges
   specific hypotheses; an embedding channel via the ProviderRegistry `embed` capability
   (cache by content hash + model). Fuse with lexical evidence; keep every decision explainable
   per channel. Measure recall/precision/traps, ranking AUC, cost and latency; then validate on
   the **blind corpus** only once at the end (§4).
2. **Jev recipes (T3)** — `loom/tests/fixtures/eval/jev_recipes_v1/`, plan in
   `docs/research/JEV_RECIPES_PLAN_2026-09-29.md`: expressed‑vs‑inferred relation, JSON key
   semantics sensitivity, string vs structured rubric, flat vs hierarchical Choice, 4/8/16
   options, the three families (relation relevance, subgraph relevance, needed detail).
   ≤184 calls. Record results into **ModelProfiles (T4)** per question/recipe.
3. **Cheap semantic model in extraction** (`loom/include/loom/knowledge_semantic.h`,
   `docs/research/SEMANTIC_MODEL_FLOW_2026-09-28.md`): the native, validated candidate path
   exists (exact‑quote grounding, candidates never promoted). Run it for real on
   synthetic_dev and on repository docs; measure decision/principle/operator recall vs the
   regex baseline (`python3 loom/tools/eval/knowledge_eval.py synthetic ...`), and the
   admissibility rate of candidates. Compare models by operation → ModelProfiles.
4. **Structure of thought on real text** (R22–R24): model‑assisted extraction of atomic
   operations (generalisation, specialisation, conditions/branches, syllogisms, analogies…)
   into the *same* graph; compare derived subgraph projections (WL, bounded alignment) for
   cross‑topic recurrence; judge candidate matches with Jev. Measure on fresh, frozen cases,
   not the ones already inspected.
5. **Refinement consolidation** (R34) with the T2 corpus/evaluator: model compiles a
   refinement block into an active task specification; score fidelity (exceptions kept, no
   return of rejected variants, no invented fixes).
6. **Context selection quality** (R12, R28): same task with naive history vs flat retrieval
   vs Loom ContextSet (scope/detail variants); score answers against the frozen spec; report
   tokens and quality separately. Wiring the ContextEngine into the chat request path
   (acceptance test #1) is a good native task if you want one.

7. **Proposal: frontier-model methods as first-class methods** (owner, 2026-09-30). You lead now;
   take, change or drop any of this.
   - **Owner principle** (also at the top of `AGENTS.md`): everything is user-configurable. That
     covers the model, scope, reasoning depth, auto-acceptance of model output, and what is sent
     where, up to "burn a billion dollars". The one exception: an expected ≥10× jump in usage
     needs the user's confirmation. Defaults are presets, not rules. My earlier "conditions"
     (frontier only on a selected part, model output never auto-promoted) were exactly such
     presets and must not be read as limits.
   - **In the abstraction of analysis methods**, consider methods that use the internal complexity
     and abilities of advanced models, next to regex, TF-IDF, embeddings and Jev. Examples:
     - give the model the graph plus a conversation and ask it to complete the graph from the
       new data;
     - look for new, unnamed patterns and structures;
     - propose type/structure definitions;
     - merge or split entities, or reorganise the graph;
     - critique the existing graph;
     - ask about gaps;
     - reason at length over a whole archive when the user wants that.
   - **Implement these methods so they are handled and configurable.** Test them offline first. Use
     live calls sparingly, where they inform a decision (funds are limited, not forbidden).
     `loom/tools/eval/archive_cost.py` (offline) gives planning numbers.
   - **Proposal: one graph exchange form, `loom.graph_packet/1`.** The same shape would serve as
     model input and output, export, preview and manual editing, so every method gets the graph
     identically:
     - `definitions`: types, predicates, roles, paradigms, operators and user-defined or
       discovered structures, each with a description, examples and origin, so a model sees what
       they mean;
     - `entities`: the nodes;
     - `claims`: the rich edges, exactly `Claim.to_json()` from `loom/include/loom/model.h`
       (subject, predicate, object or value, qualifiers, assessment, support quotes with
       locators, derivation, premises, alternatives, consequences, open questions);
     - `sources`: observations and messages with locators;
     - `task`.

     A model's answer would come back as a diff in the same shape. The existing
     `loom.candidate_graph/1` (`relation_v1` / `occurrence_graph_v1` in
     `loom/src/extract/semantic.cpp`) is a narrower, extraction-only form that could become a
     profile of this one. Message annotations (message → node, span, origin
     `recorded | model | user`) would fit the same packet.

## 4. Data and integrity rules (non‑negotiable)

- **Development data:** `loom/tests/fixtures/eval/synthetic_dev` (fictional persona) and the
  repository itself. **Blind validation corpus:** branch `wip/worktree-agent-a342fccb481c4116c`
  — do **not** read it while developing; evaluate on it once at the end and report that the
  result is the first look. **Temporal hold‑out answer key:** branch `eval/real-holdout-key`
  — never read or tune against it (the data pack also leaks later knowledge; see STATE §5).
- **Ratchet (owner D2):** no tracked test or metric may get worse; never lower a threshold or
  weaken a gate; report denominators and data provenance; development vs validation separate.
- Inference ≠ observation; model output enters as candidates with provenance, never promoted
  automatically. `model_knowledge` never outranks the owner's sources.
- Policy/variety in data (`loom/data/**`, then `python3 loom/tools/gen_kb_pack.py`), universal
  operations in code. New public ABI → exported‑symbol test.
- Cost: explicit caps per experiment; checkpoint before each request; an uncertain request is
  not retried automatically. Save first responses and ledgers under `docs/research/inputs/`.

## 5. Useful commands

```sh
# native
cd loom && cmake --preset dev && cmake --build --preset dev -j && ctest --preset dev
LOOM_CATALOG_EVAL_VERBOSE=1 build/dev/loom_tests --test-suite=catalog_eval
python3 tools/eval/knowledge_eval.py synthetic --loom build/dev/cli/loom --work /tmp/w1 --out /tmp/s1.json
python3 tools/eval/knowledge_eval.py selfhost  --loom build/dev/cli/loom --repo .. --work /tmp/w2 --out /tmp/p2
# python suites (need: pip install pytest "jsonschema>=4.18,<5" "referencing>=0.30,<1" rfc3339-validator)
python3 -m unittest discover -s loom/tools/contracts -p 'test_*.py'
python3 -m unittest discover -s loom/tools/eval -p 'test_*.py'
```
Disk: each build dir is 4–6 GB; delete stale ones.

## 6. When you finish a round

Commit small verified increments with `[skip ci]`, push to the development branch (or a
`gpt/*` branch if you prefer review), and **update `docs/STATE.md`** (date, commit, test
counts, measured numbers). Leave a short handoff for the next agent in the same style.

## 8. Proposal: parallel threads (up to 6 helper agents each) — owner, 2026-09-30

A proposal only; you lead and decide. Each thread owns a disjoint set of files so nothing
overwrites anything. Each thread works on its own `gpt/<thread>` branch and merges into the
development line only after a green `ctest` (ratchet D2). Commit small increments with
`[skip ci]` and push them. At the end of a thread, add a line to `docs/STATE.md`.
Test offline by default; live calls are allowed but funds are limited, so use them sparingly and record their cost. The owner principle at the top of `AGENTS.md` applies.

1. **Knowledge precision** — `loom/src/{extract,resolve,generalize}`, `loom/data/rules|policy`.
   - Verify and merge `wip/precision`: build, ctest, knowledge_eval synthetic + selfhost before
     and after.
   - Then the remaining error classes:
     - code fragments recognised as projects;
     - paradigm matches across unrelated domains;
     - wrong computed versions;
     - claim volume (8,928 claims for 283 files).
   - Agents: one per error class, plus one for measurement.
2. **`loom.graph_packet/1` + frontier-model methods** — new `loom/src/packet/`, `docs/contracts/`.
   - Schema, and serialization from `Claim`/`Entity` with definitions included.
   - Model answers come back as diffs in the same shape.
   - Methods, all configurable and tested on ScriptedTransport:
     - complete the graph;
     - find new patterns;
     - critique the graph;
     - ask about gaps.
3. **Context in chat + user controls** — `loom/src/{context,chat,config}`.
   - Wire ContextEngine into the chat path (acceptance #1: history ≠ request).
   - Scope and detail as separate settings (#3).
   - A ×10 usage guard that asks for confirmation, with no hard ceilings.
4. **Catalog / selector, offline** — `loom/src/catalog`, `loom/data/policy`.
   - Embedding channel via `ProviderRegistry.embed`, cached, with a fake provider in tests.
   - Semantic seam before selection.
   - Lexical-shadow regression cases (#11).
   - Do not touch the blind corpus yet.
5. **Message annotations + real-archive readiness** — `loom/src/import`, `loom/src/db`,
   `loom/tools/eval`.
   - Link message → node, with span and origin (`recorded|model|user`).
   - Streaming performance on multi-GB exports; `import --audit` + `archive_cost.py`.
6. **Workbench UI** — `loom/web`, `loom/server`.
   - Free composition of several graphs, tables, cards and a detached reference panel (#5, #13).
   - UI profiles detachable from selector and model (#6).
   - UI for the settings from thread 3.
