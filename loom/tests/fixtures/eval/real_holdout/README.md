# Real temporal holdout — answer key

> **Do not use this directory for tuning.** It is the answer key of the
> primary benchmark and must stay out of the implementers' branch until the
> final evaluation. Do not read it, grep it, paste it into prompts, derive
> lexicons, rules, seed principles or thresholds from it, or run the engine
> over a checkout that contains it. The `.loom-archive` marker file makes the
> Loom archive walker skip this directory, but that is a safety net, not
> permission.

## What this is

The benchmark requested in `docs/architecture/NOTATKA_GPT_2026-09-26.md` §9
and `docs/architecture/LOOM_CONCEPTUAL_MODEL.md` §7: give the self-discovery
engine only the owner's real sources dated ≤ T, let it induce principles,
transformation operators, the project model and **predictions of later
decisions**, and compare with what actually happened after T — at the level
of abstraction / solution class, not feature names.

Everything here is grounded in the repository, with locators
(`file:line`, `git show <sha>`, snapshot paths). Nothing is invented; where
the sources disagree or are unclear, the files say so in `ambiguity`,
`notes` or `date_basis`.

| File | Content |
|---|---|
| `timeline.json` | 149 dated units: 22 report sessions + the report, every git commit (107), the 0.8.3 and 0.9.0 snapshots, the 2026 documents, and sources known only by citation. Each has `available_at` (conservative upper bound on when the content existed) and a date-trust level. |
| `cuts.json` | 6 cut points T1–T6 with the input unit ids (`available_at ≤ T`) and the after-T answer key (51 decisions in total; T1 has all 51, T6 has 12). |
| `principles_by_time.json` | 41 principles (value / epistemic / strategy × invariant / heuristic / default / meta / conflict_resolution) and 14 operators (situation → solution class), each with dated evidence, verbatim phrasings, authorship and violations. Includes the list of owner-stated invariants later violated. |
| `lineage.json` | Code lineage: report versions, git line, `0.8.x ← 0.7.09 @46d0ba7`, `0.9.0 ← 0.7.10 @1e3fa2b` (before the `ddcab9e` fix), Loom ← repo main; per-file diff evidence. |
| `feature_status.json` | 26 features with status per branch/version and their oscillation (e.g. API editor: preview v0.4.6 → partial 4-tab v0.06.02 → TODO stub 0.7.x → raw JSON 0.8.3 → stub 0.9.0 → absent in Loom). |

### Dates you must not trust

Git versions 0.2.0–0.7.9 were committed in one evening (2026-02-11,
20:06–21:11 +0100, about one per minute): the commit dates are an import
artefact. Content dates for 0.2.0–0.06.03 come from the report (git 0.6.3 has
exactly the report's 5738 lines); 0.7.0–0.7.9 lie between 2026-02-08 and
2026-02-11. `6ab4366` and `ddcab9e` are the owner's cherry-picks of `1e3fa2b`
(0.7.10, 2026-02-11) and `287bdc1` (fix, 2026-03-06). Git version labels also
disagree with the report (git "0.3.0" is the GPT prototype, not report v3).
Report sessions 18–21 have header date 2026-02-06 but transcripts named
2026-02-08. The 0.8.3 date (2026-03-17..21) comes only from archive file
dates. See `timeline.json` → `git_date_findings`.

## Cuts

| Cut | T | What the engine gets | Answer key |
|---|---|---|---|
| T1 | 2026-02-08 | report sessions 1–22 + git content of 0.2.0 … 0.06.03 | 51 (20 predictable / 27 partially / 4 surprising) |
| T2 | 2026-03-06 | + 0.7.0 … 0.7.10, CLAUDE.md, abspath fix | 39 |
| T3 | 2026-03-21 | + 0.8.x snapshot | 36 |
| T4 | 2026-04-18 | + 0.9.0 snapshot | 27 |
| T5 | 2026-09-16 | + MEGA MASTER, the owner's 09-16 commits | 17 |
| T6 | 2026-09-26 01:10Z | + Loom waves 1–3 (before the recovered snapshots upload and R1–R13) | 12 |

## How to evaluate

1. **Inputs ≤ T only.** Build the engine's corpus from `cuts[i].inputs`
   (resolve each unit id through `timeline.json` → `locator`). For git units
   give the tree/diff of that commit, not the repository history: a checkout
   exposes later commits, messages and dates. Snapshots are available by
   content date even though they were uploaded on 2026-09-26.
2. **No hindsight in the engine's own data.** `loom/data` (kb pack, 2026-09-26)
   was written with knowledge of the whole history: `philosophy/seed_principles.json`
   cites MEGA MASTER (2026-09-16) in 34 of 43 sources, `profiles/self.json` is
   seeded from MEGA MASTER, and `rules/inference_rules.json:223` names the
   0.8.x/0.9.0 fork example. For cut T, remove every pack element whose sources
   are dated > T (for T1–T4 that removes almost all seed principles), or run
   with a neutral pack. Also report a no-pack ablation. `model_knowledge`
   (general LLM knowledge) is allowed but must not come from a model that has
   seen this repository.
3. **The engine must output**, per cut: principles (statement, level, form,
   evidence), operators (situation → solution class) and a list of
   predictions `{situation, predicted solution class, supporting
   principles/operators, confidence, horizon}`. Predictions are frozen before
   the answer key is opened.
4. **Matching at solution-class level.** A prediction matches a key entry when
   a reviewer (or a pre-registered LLM judge with the key hidden from the
   engine) agrees that it names the same *class* of solution in the same
   situation — "generalise long-running remote operations into one job
   abstraction" matches D17 whatever the classes are called; "add a video
   panel" does not match D16. One prediction matches at most one entry; the
   judge records the reason.
5. **Scores per cut** (report all, never only an aggregate):
   - *recall* of `predictable` entries and of `partially` entries separately;
     `surprising` entries are reported but not penalised when missed (a hit on
     one is a strong result and must be checked for leakage);
   - *precision*: share of predictions that match any later decision (all
     decisions after T, not only the key's horizon);
   - *horizon*: `next` (before the following cut) vs `later`;
   - *plan-following vs induction*: entries with `stated_before_T: true` were
     explicitly requested/planned in ≤ T sources; count them separately so
     that retrieval of stated plans is not scored as induction;
   - *calibration*: ECE of the engine's prediction confidences against
     matched/unmatched;
   - *principle recall*: principles in `principles_by_time.json` with
     `first_evidence.date ≤ T` that the engine recovered (statement-level
     match), and *typing accuracy* of level/form;
   - *false-certainty* (hard gate, conceptual model §7): an inferred principle
     or prediction presented as observed.
   - lineage and status: compare with `lineage.json` / `feature_status.json`
     only with inputs ≤ the version's date; oscillation (`lost` →
     `restored` → `lost`) must be kept, not collapsed.
6. **Ambiguity.** Entries or evidence marked `ambiguity`, a `date_trust` of
   `derived`/`medium`, or authorship `agent`/`external_llm` are not errors in
   the key: if the engine's answer differs on an ambiguous point and it states
   the ambiguity, score it as correct; if it asserts one side as observed
   fact, score it as false certainty. Violations marked ambiguous (e.g. Loom
   `0.1.0` vs the x.yy.zz rule, geometric Unicode glyphs) count neither for
   nor against.
7. **Ratings are part of the key, not targets.** `predictable` / `partially` /
   `surprising` records what the ≤ T evidence supports (criteria in
   `cuts.json` → `description`). An engine that predicts a `surprising` entry
   without leakage has found something the key's authors did not.

## Authorship of evidence

Principles carry `author` per evidence item: `owner` (the owner's own words,
often quoted second-hand by the report), `owner_repo` (the owner's commits),
`report` (the report author's account of a session), `agent` (assistant-written
code/docs the owner accepted), `external_llm` (ChatGPT/Gemini text the owner
forwarded). The preset texts of 0.2.0 (epistemic/research/legal rules) were
generated at the owner's request and kept as the default; treat them as
owner-accepted, not owner-written.

## Provenance of this key

Built on 2026-09-26 from the repository at commit `e8de3a7`
(`claude/chataddhd-cpp-loom-core-IRGRN`): the report, `git log`, both
snapshots (per-file diffs recomputed), MEGA MASTER, owner requirements,
the GPT note, ANALIZA and the conceptual model. Code facts were checked in
the code (e.g. the v0.06.03 API editor did not send the edited request,
voice recording is a placeholder from 0.7.0, 0.9.0 still ships
`PLACEHOLDER/*` models while its README says placeholders were removed).
