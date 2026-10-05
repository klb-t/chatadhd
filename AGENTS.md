# Instructions for coding agents (Codex / Claude / others)

Paid model calls (owner, 2026-10-04): allowed, funds are limited — use them wisely.
The owner authorised **EUR 5 on a separate OpenRouter key** for thread 7 (model
research), covering cheap-model prompt/parameter studies, a small frontier-model
panel (graph completion, pattern discovery, graph reply vs text+JSON) and
repeats/unseen checks. Plan and cost estimate before each stage and the actual
cost after it. The key is never committed. Supersedes the earlier USD 2 limit and the
"no frontier pilot" note in `docs/research/OWNER_CLARIFICATION_2026-09-30.md`.

> **ZASADA WŁAŚCICIELA (2026-09-30), OBOWIĄZUJE ZAWSZE:** NIE PODEJMUJEMY DECYZJI ZA UŻYTKOWNIKA,
> ZWŁASZCZA OGRANICZAJĄCYCH. WSZYSTKO JEST KONFIGUROWALNE: MODEL, ZAKRES, ROZUMOWANIE, AUTOMATYCZNE
> PRZYJMOWANIE WYNIKÓW, CO WYSYŁAMY. USTAWIENIA MUSZĄ POZWALAĆ PRZEPALIĆ NAWET MILIARD DOLARÓW.
> JEDYNY WYJĄTEK: SPODZIEWANY WZROST ZUŻYCIA O RZĄD WIELKOŚCI (×10) POTWIERDZA UŻYTKOWNIK.
> DOMYŚLNE WARTOŚCI TO PRESETY, NIE REGUŁY. NIE WYMYŚLAJ OGRANICZEŃ.

**Start at `docs/STATE.md`** — the single canonical, dated state of the project
(what works, verified numbers, open work, what is needed from the owner).

Then read, in order: `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md`
(the owner's requirements, verbatim), `docs/architecture/LOOM_CONCEPTUAL_MODEL.md`
(the one vocabulary; one meaning per term), `CLAUDE.md` (build, tests, the
Python↔C++ compatibility invariant), `loom/README.md`.

## Rules that have proved necessary

- Keep the owner's options open; policy and variety live in **data**, code holds
  universal operations. String literals in product code are limited to the six
  categories of R42 (contract keys/schema ids, external standards, mechanism
  vocabulary, contract serialization formats, data bootstrap, developer
  diagnostics). Test: if someone could want to change it without changing the
  algorithm, it is data. Inference is never presented as observation; the owner's
  judgement always wins; missing capability => lower evidence class, never a
  fabricated metric.
- Preserve all source bytes; derived state must be rebuildable.
- Python compatibility is NOT required any more (owner, 2026-09-29: it was a
  minimum plan). Existing compat tests stay green as sentinels until a deliberate
  migration; schema changes must be deliberate (migration + note in STATE.md).
  Every public ABI addition needs the exported-symbol test.
- Develop linearly: one line, ratchet — never worsen a tracked test/metric.
- Never weaken a quality gate to make it pass. Report honest numbers with their
  denominators, data provenance and caveats; keep development, independent and
  holdout data separate. Do **not** read or tune against `eval/real-holdout-key`.
- Commit small, verified increments and push them (sessions and limits end
  abruptly). Use `[skip ci]` until GitHub Actions minutes reset (2026-10-01).
- Never commit credentials. Live model calls only with an explicit budget cap
  and saved first responses.
- Update `docs/STATE.md` (with date, commit and test counts) at the end of every
  session. Historical logs live in `docs/research/` and `docs/*_2026-09-28.md`.
