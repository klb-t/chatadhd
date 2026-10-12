# B-ECO-01 — Siedem repozytoriów i audyt 2026-10-09 jako dane grafu

| Pole | Wartość |
|---|---|
| Repo / baza | `klb-t/chatadhd`, gałąź robocza po scaleniu S2; audyt: `origin/gpt/ecosystem-audit-2026-10-09` |
| Fala / priorytet | W1 / P1 (tanie, odblokowuje śledzenie wszystkich pozostałych briefów) |
| Ustalenia | brak własnych; realizuje EITG §4 wiersze „kod i repozytoria”, „samomodel”, „dane innych repozytoriów”; EITG D-G6 |
| Niezmienniki EITG | G1, G2, G6, §7 koperta |
| Rozmiar / model | S / Sonnet 5.5 high; przegląd Opus 5.5 |
| Zależności | żadne z T1–T8 (wyłącznie nowe pliki); T3 może od razu konsumować wynik w obszarze „Technologie i adaptery” |
| Worktree | `t9-eco-01` |

## Zakres
1. Siedem repozytoriów ekosystemu jako węzły z relacjami z `relationships.json` audytu
   (`contains_active_component`, `preserved_compatibility_predecessor_of`, `current_product_name`,
   `research_instrument_for`, `distinct_product_with_components`, `possible_collaboration_only`,
   `retained_history_not_current_product`, `separate_private_repository`).
2. Ustalenia audytu (65 z `findings.jsonl` + 27 z `pass4/findings.jsonl`) jako twierdzenia
   o repozytoriach z kopertą `loom.ecosystem_ref/1`, klasą dowodu `observed`, `origin=repo`,
   `known_at` = data audytu, status z audytu (naruszenie / mechanizm / niepewne / naprawione).
3. Standalone `loom` jako `preserved_compatibility_predecessor_of` z adnotacją „nie otwiera
   nowszych formatów” (rekomendacja LOOM-005).
4. `ar-drafty` wyłącznie jako węzeł z kopertą do prywatnego raportu (SHA `d8af2dd0…`), bez treści.

## Pliki
- Nowe dane: `loom/data/ecosystem/repos.pack` (schemat `loom.ecosystem_repos/1`: lista repo,
  SHA bazowe audytu, relacje, koperty), wpis w `loom/data/data_domains.pack` z `sha256`
  i `validation_refs`.
- Kopia źródeł audytu na gałąź roboczą (same dokumenty): `docs/reports/ecosystem-audit-2026-10-09/`
  (`findings.jsonl`, `pass4/findings.jsonl`, `relationships.json`, `README.md`, `backlog.md`,
  `backlog.json`, `pass4/REPORT.md`, `pass4/backlog.json`); bez ZIP-ów i bez `scan/*.gz`.
- Generator: `loom/tools/gen_ecosystem_claims.py --check` czytający te pliki i emitujący
  `loom/data/ecosystem/audit_claims.pack` (schemat `loom.ecosystem_claims/1`); osadzenie jak inne
  generatory (`gen_runtime_profiles.py`), nigdy ręczna kopia.
- Test: `loom/tests/test_ecosystem_repos.cpp` (doctest) albo `loom/tests/compat/test_ecosystem_claims.py`
  (patrz kryterium).

## Mechanizm docelowy (istniejący)
`data_domains.pack` (inwentarz domen danych z digestami), KnowledgeStore (twierdzenia z `Assessment`),
`profiles/self.json` (samomodel), koperta z EITG §7, która jest formalizacją pól `repo`/`sha`/`file`/
`line_start`/`line_end`/`range_sha256` już używanych w `findings.jsonl`. Żadnego nowego magazynu.

## Kroki
**Rozważenie.** Potwierdzić, że `data_domains.pack` przyjmuje nową domenę bez zmiany loadera
(`test_kb_data_domains.cpp`); sprawdzić, czy KnowledgeStore ma predykaty wystarczające do
twierdzeń „repo R ma ustalenie F w pliku P” (jeśli nie — predykat jest danymi w `schema/types.json`,
nie kodem). Zapisać rekord decyzji z wyborem: twierdzenia ładowane jako seed KB (R52) czy jako
osobny pack konsumowany przez widok wejściowy — preferowane drugie, bo nie miesza audytu
z wiedzą użytkownika.

**Implementacja.** (a) kopia dokumentów audytu; (b) `repos.pack` ręcznie z `relationships.json`
(8 relacji, 7 repo); (c) generator twierdzeń z `--check`; (d) wpisy `data_domains.pack`;
(e) test; (f) jedna linia w `profiles/self.json` wskazująca `ecosystem/repos.pack` jako źródło
obszaru „repozytoria” (bez zmiany kształtu `self.json`).

**Optymalizacja.** Zmierzyć rozmiar `audit_claims.pack`; jeśli > 200 KB, trzymać tylko kopertę
i tytuł, a pełny rekord czytać na żądanie z `findings.jsonl` (materializacja `reference`, EITG §1).

**Iteracja.** Każdy zamknięty brief T9 dopisuje rekord decyzji do `decisions.jsonl` w repo, którego
dotyczy; generator włącza go jako twierdzenie `superseded_by`/`fixed_at` z nową kopertą.

## Kryterium zamknięcia
- Dla każdego ustalenia `chatadhd` test odtwarza `range_sha256` z `git show <sha>:<file>` dla
  podanego zakresu linii i porównuje: 100 % zgodności albo jawna lista rozjazdów (audyt mógł się
  pomylić — rozjazd jest wynikiem, nie błędem testu).
- Dla innych repozytoriów koperty mają `verified: false` (brak checkoutu); test sprawdza wyłącznie
  kompletność pól i format SHA/ścieżek.
- `gen_ecosystem_claims.py --check` zielony; `test_kb_data_domains` zielony; pełna brama `chatadhd`.
- `ar-drafty`: zero pól poza `repo`, `revision`, `path` raportu i statusem `private`.

## Czego nie robić
- Nie klonować innych repozytoriów i nie łączyć się z siecią.
- Nie przepisywać ustaleń własnymi słowami: tytuł i klasyfikacja dosłownie z audytu.
- Nie zaliczać ustaleń jako naprawionych na podstawie `STATE.md`; status zmienia dopiero rekord
  decyzji briefu, który je zamknął.

## Co zapisać
Rekord decyzji w `docs/reports/t9-briefs/decisions/B-ECO-01.jsonl`; liczby: repo, relacje,
twierdzenia, rozjazdy hashy, rozmiar packa.
