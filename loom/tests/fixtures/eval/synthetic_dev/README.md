# synthetic_dev — SYNTHETIC eval corpus (fictional persona, not real data)

**Everything in this directory is about a wholly FICTIONAL person, "Ola
Testowa", and FICTIONAL projects.** It is standard test-fixture engineering
for Loom's Archive Intelligence / self-discovery pipeline
(`docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` R1, R2, R4, R5, R10;
`docs/architecture/NOTATKA_GPT_2026-09-26.md` §9;
`docs/architecture/LOOM_CONCEPTUAL_MODEL.md`). Nothing here is the real repo
owner's data, words or history. The corpus is structurally isomorphic to the
kind of multi-project, multi-year, bilingual mess Loom must handle in real
archives — content, names, projects, dates, and phrasing are all invented —
so that an eval harness has **exact ground truth** to score against.

## Why this exists

The owner asked Loom to find, inside multi-GB ChatGPT/Claude export
archives, everything about a person's projects and reasoning — catalog,
versions, statuses, forks, principles, transformation operators — without
importing everything, and to mark inferred data honestly (R1, R2, R4, R5,
R10). Measuring that needs export archives with **exact** ground truth. The
owner's real archives aren't available for this and must not be imitated, so
this corpus is about a clearly fictional person and fictional projects that
are structurally isomorphic to the real thing (`LOOM_CONCEPTUAL_MODEL.md`
§7.3: "Synthetic corpora for scale and precision").

## Files (all committed, all small)

| File | What it is |
|---|---|
| `chatgpt_export.zip` | A ChatGPT-style export: `conversations.json`, a JSON array of `mapping`-tree conversations (`create_time` epoch floats, `current_node`, branch forks via sibling nodes — matches `loom/src/archive/ingest_parse.cpp::walk_chatgpt`). |
| `claude_export.zip` | A Claude-style export: `conversations.json` (`chat_messages`/`sender`/`created_at`, two conversations use `parent_message_uuid` to represent a branch — matches `walk_claude`), `projects.json` (2 Claude Projects with `description`/`prompt_template`/`docs`), `memories.json` (`conversations_memory` + `project_memories`). |
| `ground_truth.json` | Exact ground truth, expressed in `LOOM_CONCEPTUAL_MODEL.md`'s own terms. Schema documented below. |
| `README.md` | This file. |

Regenerate everything with:

```bash
python3 loom/tools/gen_synthetic_eval_corpus.py
```

The script is fully deterministic (no wall-clock, no randomness anywhere)
and self-checking: it asserts every `decision`/`principle`/`feature`/`area`
id referenced by a message tag actually resolves, and every decision has a
locator, before it writes anything. Two other generators build things that
are **not** committed (produced fresh at test/eval time instead — see their
own docstrings for why):

```bash
# A tiny fictional git repo (NoteFlow's code/"self" side), commit history
# mirroring the corpus's version/fork timeline exactly (same source of
# truth, imported directly — the two cannot drift apart). Deterministic:
# byte-identical commit hashes on every machine, every run.
python3 loom/tools/gen_synthetic_fake_repo.py --out /tmp/noteflow-fake-repo

# Pad the ChatGPT export to a target size for multi-GB scale testing (R1).
# Streaming, deterministically seeded, starts from the real corpus so
# ground_truth.json stays checkable against the padded file.
python3 loom/tools/scale_archive.py --target-gb 2 --out /tmp/big/conversations.json
```

## The persona and its 5 fictional projects

**Ola Testowa** — an indie developer, bilingual PL/EN, working on several
side projects at once, scattered and honest about it. Her own
**philosophy** (13 principles, 6 transformation operators — see below) is
**deliberately partly different** from the real owner's seed principles in
`loom/data/philosophy/seed_principles.json`: some ideas are independently
convergent (same shape, her own phrasing/scope/evidence), and at least one
(`pr.decide_fast_correct_later`) is in explicit, recorded tension with a
seed principle (`p.defer_decisions`) — a corpus that only ever agreed with
the seed pack would let a lazy discovery engine cheat by copying seed priors
instead of reading the archive.

| Project | Kind | Shape |
|---|---|---|
| **NoteFlow** (`proj.noteflow`) | `multiplatform_app` | Notes+chat app, versions 0.1.0 → 1.2.0, Python → C++ core. Forks at 0.6.0 into `python-quick` (abandoned, 3 commits, never merged) and `cpp-core` (becomes `main`). Two features oscillate (voice transcription: implemented → lost → restored; checklists: implemented → lost → restored → lost again). Two versions (1.0.1, 1.0.2) exist **only** in the fake git repo, mentioned in no conversation. |
| **Reeltime** (`proj.reeltime`) | `pipeline` | Staged text → storyboard → keyframes → clips → compose reel generator. Never finishes; an open cost question sits unresolved for 8 months, then gets a cost-gate decision right after the temporal cut. |
| **Stroz / Watchdog** (`proj.watchdog`) | `agent_system` | Watches a long coding-agent session, restarts on hang, summarises, alerts. Built only after two weeks of hand-logged manual restarts. |
| **Sprawa Kwiatowa / Lokatorka** (`proj.lokatorka`) | `legal_case` | A fictional tenancy dispute (invented statute, invented court, invented landlord and lawyer — not real legal citations). Deadlines computed from dated events, one real contradiction (a dated e-mail vs. a later denial), one reversed strategy. |
| **Analog Ghosts** (`proj.analogghosts`) | `music` | A solo lo-fi/synth EP, two finished tracks, one abandoned-but-kept track, a key decision deliberately left open for months then resolved. |

`legal_case` and `music` have **no** paradigm data file yet under
`loom/data/paradigms/` — deliberately: their universal-role evidence in
`ground_truth.json` is expressed directly against the 14 universal roles
(`LOOM_CONCEPTUAL_MODEL.md` §3.4), testing whether discovery degrades
gracefully to role-level matching for a project kind with no pre-seeded
paradigm.

## `ground_truth.json` schema (`loom.eval.ground_truth/1`)

Top-level keys:

- **`persona`** — the fictional-persona disclosure.
- **`temporal_cut`** — `date` (`2026-05-01`) and rationale for the
  predictive-reconstruction benchmark (`LOOM_CONCEPTUAL_MODEL.md` §7.1).
- **`units`** — `relevant` (45 signal conversations, each with `project`),
  `noise_traps` (5), `noise_generic` (15). Together with `counts` this is
  the answer key for selection recall/precision and noise-trap
  false-positive rate.
- **`cross_project_alias_mentions`** — locators where one conversation
  name-drops several projects by an alias/nickname (entity-resolution
  ground truth: e.g. NoteFlow is also called "appka od notatek", Watchdog
  "Stróż").
- **`fork_chat_units`** — locators for the ChatGPT sibling-node forks tagged
  during authoring (the edit-fork demos; the two Claude
  `parent_message_uuid` forks are inside `projects[].versions`/decisions
  context instead, since both double as real decision points).
- **`projects[]`** — `id`, `name`, `kind`, `aliases`, `universal_roles`
  (per role: `observed` — locator + quote from a tagged message —,
  `inferable` — `value` + `expected_property` (a checkable predicate, R5)
  + `inference_basis`, and `absent` — a named gap, never silently
  omitted), `versions`, `forks` (base version, branches, outcome,
  `resolved_by_decision`), `decisions` (alternatives, chosen,
  `affected_values`, `supersedes`/`supersedes_branch`, `principle_evidence`,
  `operator`), `features_status` (**status per branch+version**, including
  the two lost→restored[→lost again] oscillations), `areas`,
  `open_questions`.
- **`principles[]`** — `id`, `level` (`value`\|`epistemic`\|`strategy`),
  `form` (`invariant`\|`heuristic`\|`default`\|`meta`\|`conflict_resolution`),
  bilingual `statement`, `protects`, `scope`, `conflicts_with` (references
  seed-pack ids directly where deliberate), `derived_from`,
  `validation_status`, and `phrasings[]` — every tagged occurrence across
  conversations **and** the two Claude Project docs, each a full locator
  (provider, conv/doc id, node id, date) + the exact text.
- **`operators[]`** — `situation` → `solution`, justifying `principles`,
  and `examples` (one `pre_T` and one `post_T` decision each — the
  temporal-holdout pairs).
- **`areas[]`** — brainstorm generalizations: `generalization_pl/en`,
  `listed_members`, `inferable_members` (each with `expected_property` +
  `inference_basis`, R10), and `unit` (the locator of the brainstorm
  message that states it).
- **`open_questions[]`** — `text`, `project`, `resolved_by` (a decision id,
  or `null` if still open at corpus end).
- **`contradictions[]`** — two claims, a `status`
  (`contested_then_resolved` for the real one;
  `resolved_not_a_real_conflict` for a deliberate false-positive control —
  scoping the area statement correctly should stop an engine from flagging
  a normal opt-in cloud sync as violating "nothing in the cloud").
- **`predictions[]`** — the 6 operator-derived temporal-holdout pairs,
  restated as `before_T_decision` → `predicted_solution_class` →
  `actual_decision` (`matched: true` for all 6 — see below for the
  negative control).
- **`unpredictable_post_T_decisions[]`** — a deliberate negative control:
  one post-T decision (`dec.nf.encryption`'s specific cipher choice) that
  is **not** predictable from any recorded operator, with a stated reason,
  so the benchmark cannot be gamed by assuming every post-T decision must
  match some operator.
- **`claude_project_and_memory_evidence[]`** — a few hand-listed
  locators into the 2 Claude Project docs and the memories object.
- **`counts`** — everything below, computed straight from the same data
  structures (never hand-typed, so it cannot drift from the files).

## Deliberate design choices

- **Locators are source-level, not Loom-internal.** Every ground-truth
  reference is `{provider, conv_id, node_id, date}` — the export's own ids
  (I control them: short readable strings like `u1`, `a2b`), not Loom's
  content-hashed internal ids. This keeps `ground_truth.json` directly
  greppable against the raw export JSON, and independent of exactly how a
  given Loom version happens to hash/assign its own ids.
- **One authoring pass, not two.** `gen_synthetic_eval_corpus.py` tags
  individual messages while authoring the dialogue (`{"principle": ...}`,
  `{"decision": ...}`, `{"feature": [...]}`, `{"area": ...}`, `{"role":
  [...]}`, `{"contradiction": (...)}`, `{"open_question": ...}`, `{"trap":
  {...}}`, ...); `ground_truth.json` is built entirely by scanning those
  tags. Export and ground truth are generated from the same run and cannot
  independently drift.
- **Messiness is concentrated in filler turns.** Real exports have typos,
  dropped diacritics, dictation garble, and long drifting threads — this
  corpus has all of those (see `nf-09-lost-and-billed`, `cx-03-drifting-thread`,
  the noise conversations) — but the specific sentence tagged as a
  principle-phrasing or decision locator stays legible. An eval fixture
  should test retrieval/classification under realistic noise, not
  OCR-level text corruption of its own answer key.
- **Not every post-T decision is predictable.** See
  `unpredictable_post_T_decisions` above — a corpus that always rewards
  "operator fired → decision matched" would be gameable.
- **Two versions live only in the fake repo.** `NoteFlow` 1.0.1/1.0.2 are
  never mentioned in any conversation, testing that code-lineage-only
  evidence gets picked up even with zero chat corroboration.
- **A false-positive contradiction control.** `contra.noteflow.sync_vs_local`
  looks like a conflict (a "no cloud without consent" area statement vs. a
  sync feature that does leave the device) but resolves once correctly
  scoped — testing over-eager contradiction detection, not just recall.
- **Noise traps collide only lexically.** `loom` (weaving), `pipeline`
  (oil), `agent` (real estate), `watchdog` (a hardware timer), `ADHD`
  (a child's medical diagnosis) each reuse a word from a signal project's
  own vocabulary in an unrelated real-world sense, with zero shared
  entities/context — a plain keyword filter should false-positive on these;
  a real selector should not.
- **`scale_archive.py`'s noise is intentionally NOT more ground truth.**
  It pads with generic, unlabelled filler so that padding a real archive to
  multi-GB scale doesn't quietly grow the answer key — the padded file's
  ground truth is still exactly this `ground_truth.json`.

## Counts (from the current `ground_truth.json`)

| | |
|---|---|
| Conversations (total / signal / noise-generic / noise-trap) | 65 / 45 / 15 / 5 |
| Conversations by provider (ChatGPT / Claude) | 34 / 31 |
| Messages (total, across all conversations) | 258 |
| Projects | 5 |
| Principles / phrasing occurrences | 13 / 32 |
| Transformation operators | 6 (each with a pre-T and a post-T example) |
| Decisions | 21 |
| Tracked features (status-per-branch-version entries) | 18 |
| Brainstorm areas (with inferable unlisted members) | 5 |
| Open questions (resolved / total) | 2 / 5 |
| Contradictions (real / false-positive control) | 1 / 1 |
| Temporal-holdout predictions (all matched) + 1 negative control | 6 + 1 |
| Claude Projects | 2 |
| Code-lineage forks / structural chat forks | 1 / 4 |

Re-run `python3 loom/tools/gen_synthetic_eval_corpus.py` and read
`ground_truth.json`'s own `counts` key for the authoritative numbers if this
table and the file ever disagree — the file is the source of truth.
