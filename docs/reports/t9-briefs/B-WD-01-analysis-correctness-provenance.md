# B-WD-01 — Watchdog: poprawność liczb i pochodzenie w analizie (`missingPolicy`, manifest ponownej analizy)

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/Watchdog-JH16` @ `58a0c93b` (`main` 2026-10-09); `STATE.md` 2026-10-09: 644/644 po `6be9aed` (A4-WD001) i `f2b6b89` (A4-WD003) — **potwierdzić lokatorami na bieżącym `main`** |
| Fala / priorytet | W1 / P1 |
| Ustalenia | WD-002, A4-WD-002; weryfikacja statusu: WD-001, WD-003, WD-011, A4-WD-001, A4-WD-003 |
| Niezmienniki EITG | G2, G8, G9 |
| Rozmiar / model | S / Sonnet 5.5 high |
| Zależności | brak (osobne repo) |
| Worktree | `t9-wd-01` |

## Zakres
1. `ratio` z `missingPolicy=exclude` wyklucza brakujący wiersz; `propagate` zachowuje `null`;
   `fail` odmawia — albo walidator jawnie odrzuca `exclude` i dokumentuje brak zdolności (WD-002).
   Maska wykluczeń w śladzie wyniku.
2. Manifest i eksport ponownej analizy zachowują braki i flagi wejścia (A4-WD-002).
3. Dla WD-001/003/011 i A4-WD-001/003: uruchomić testy pass4 i sondy audytu na bieżącym SHA
   i zapisać status (naprawione / nadal wadliwe) z lokatorem — bez pracy, jeśli PASS.

## Pliki (lokatory audytu @ `58a0c93b`)
- `backend/watchdog_api/analysis/primitives.ts:137–180` (`ratioPrimitive`).
- `backend/watchdog_api/analysis/method_spec_validation.ts` (`validateMethodSpec`),
  `analysis/executor.ts:85–88`.
- A4-WD-002: runner `tools/ecosystem-audit-2026-10-09/pass4/watchdog/run.py --repo CHECKOUT --sha SHA --out RECEIPT.json`,
  przypadek `A4-WD-002.B`; raport `pass4/watchdog/REPORT.md`.
- Weryfikacja: `services/narrative.ts:85–122` (WD-001), `:143–175` (WD-011),
  `llm/profile_generator.ts:11–22` (WD-003); przypadki `A4-WD-001.B`, `A4-WD-003.B`.

## Mechanizm docelowy (istniejący)
`MethodStep.missingPolicy` (już w kontrakcie `MethodSpec`) konsumowany przez prymityw; ślad
wykonania z maską wykluczeń; manifest eksportu z polami pochodzenia wejścia. Żadnego nowego
magazynu ani zmiany „locked JH16” (ryzyko audytu: nowa wersja executor/primitive).

## Kroki
**Rozważenie.** Potwierdzić lokatory; uruchomić istniejące testy (53 w audycie; 644 w `STATE.md`)
i runner pass4; rekord decyzji z tabelą statusów pięciu weryfikowanych ustaleń.

**Implementacja.** (a) test jednostkowy `[null, 4] / 2`: `exclude` → wyłącznie `b=2`; `propagate`
→ `a=null, b=2`; `fail` → odmowa; (b) prymityw honoruje politykę; maska w śladzie; (c) jeśli
zmienia się liczba wierszy w dalszych krokach — **nowa wersja** prymitywu/executora z hashem,
stare wyniki niezmienione; (d) manifest ponownej analizy: pola braków i flag wejścia; test
`A4-WD-002.B` PASS.

**Optymalizacja.** Jedna reprezentacja maski braków używana przez `ratio`, dalsze łączenie
szeregów i eksport.

**Iteracja.** Pozostałe prymitywy z tym samym kontraktem `missingPolicy` (agent liczy, ile ich jest).

## Kryterium zamknięcia (WD-002, A4-WD-002)
- Trzy przypadki `missingPolicy` z audytu PASS; maska wykluczeń widoczna w śladzie.
- `A4-WD-002.B` PASS; `A4-WD-001.B`, `A4-WD-003.B` PASS albo udokumentowane nadal FAIL z lokatorem.
- Lint/typecheck i wszystkie istniejące testy zielone; liczba testów nie maleje.

## Czego nie robić
- Nie zmieniać hashy historycznych wyników; nowa semantyka = nowa wersja.
- Nie zaliczać WD-001/003/011 jako naprawionych na podstawie `STATE.md` bez testu na SHA.

## Co zapisać
Rekord decyzji `docs/reports/decisions/B-WD-01.jsonl` w repo Watchdog (postać `decisions.jsonl`
audytu); tabela statusów pięciu weryfikacji.
