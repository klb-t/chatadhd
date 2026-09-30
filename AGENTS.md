# Instructions for coding agents (Codex / Claude / others)

Current direct-owner clarification: `docs/research/OWNER_CLARIFICATION_2026-09-30.md`.
Continue the already authorised cheap/Jev experiments within the existing USD 2
non-resetting budget; no newly invented paid frontier pilot. The older blanket
paid-synthetic restriction in the Claude handoff is superseded by this correction.

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
  universal operations. Inference is never presented as observation; the owner's
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
