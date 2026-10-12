# B-CKP-02 — IO Matrix: Policy Matrix do runtime, polityka sugestii z zapisem straty, GoalPrompt jako wersja

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/Custom-Keyboard-Pro` @ `192f820f`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W2 / P2 |
| Ustalenia | CKP-A-003, CKP-A-005, CKP-A-007 |
| Niezmienniki EITG | G2, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-CKP-01 (resolver warstw istnieje) |
| Worktree | `t9-ckp-02` |

## Zakres
- Planner: wagi, `maxSteps=5`, kara `rank*2`, koszt `null` = MEDIUM, automatyczne `plan.best`
  → profil `ConversionPolicy` z wersją; strategie kosztu nieznanego `ask` / `penalty` / `interval` /
  `deny` z `unknown` w śladzie; wszystkie trasy pokazane w wybranym zakresie; wybór trasy wpływa
  na wykonanie z uzasadnieniem (CKP-A-003).
- Sugestie: profil `AI-first` / `local-first` / `score` z dokładną kolejnością źródeł; surowy wynik
  wieloliniowy zachowany z zapisem straty (`keep` / `truncate`); kolizja tekstu z kilku źródeł
  zachowuje pochodzenie wszystkich (CKP-A-005).
- `GoalPrompt` jako wersjonowany prompt z jednym limitem alternatyw czytanym przez prompt, parser
  i `GoalAgent`; prompt nie może zmienić autoryzacji lokalnego wykonawcy (CKP-A-007).

## Pliki
- `app/src/main/java/com/example/core/matrix/Planner.kt:4–36` (`price/search/plan`);
  `app/src/main/java/com/example/io/ConvertRunner.kt` (`convert`).
- `app/src/main/java/com/example/core/suggest/SuggestionEngine.kt:131–240`
  (`collectLocal/mergeAi/cleanCompletion`).
- `app/src/main/java/com/example/core/assistant/GoalPrompt.kt:5–44`;
  `app/src/main/java/com/example/assistant/GoalAssistantActivity.kt` (`modelPlan`);
  `app/src/main/java/com/example/core/assistant/GoalAgent.kt` (`checkedPlans`).

## Mechanizm docelowy (istniejący)
Istniejący `Policy` (audyt: „podłączyć istniejący Policy do ustawień/profili”); kaskada ustawień
(CKP-A-010); katalog zasobów z B-CKP-01. Autoryzacja wykonawcy i walidatory pozostają niezależne
od tekstu promptu (CKP-A-009: mechanizm).

## Kroki
**Rozważenie.** Potwierdzić lokatory; sprawdzić, które pola `Policy` już docierają
(`allowLeavingDevice`, `through`, `avoid` — tak; wagi — nie); rekord decyzji: obecny ranking
= preset `legacy` (ryzyko audytu: zmiana kolejności psuje pamięć ruchową użytkownika).

**Implementacja.** (a) test: zmiana wyłącznie profilu wag → inne uporządkowanie tras; (b) koszt
nieznany jako strategia; `unknown` w śladzie; (c) UI pokazuje trasy w zakresie, wybór wpływa na
`execute` z uzasadnieniem (`plan.best` to preset, nie konieczność); (d) sugestie: profil
kolejności, zapis straty przy `cleanCompletion`, pochodzenie przy kolizji; test opóźnionych
wyników i braku wysyłki ze stanu `sensitive`; (e) `GoalPrompt` z `assets/`, wersja/hash; limit
alternatyw w jednym miejscu; test: dwa prompty → dwa udokumentowane żądania bez rekompilacji.

**Optymalizacja.** Jeden plik profilu (planner + sugestie + prompt) albo trzy w tym samym
katalogu — decyzja agenta z zapisem; pomiar: ile wartości opuściło Kotlina.

**Iteracja.** Expert UI pokazuje efektywny profil i uzasadnienie (B-CKP-03).

## Kryterium zamknięcia (CKP-A-003, CKP-A-005, CKP-A-007)
- Testy akceptacyjne z audytu dla trzech ustaleń PASS; domyślny profil zachowuje aktualne wyniki.
- Prompt nie może zmienić autoryzacji wykonawcy (test negatywny).
- `validate_profiles.py` PASS; JVM testy zielone albo jawnie niedostępne.

## Czego nie robić
- Nie zmieniać domyślnej kolejności sugestii (pamięć ruchowa).
- Nie czynić limitów przestrzeni planów sufitem — ustawienie z obecną wartością.

## Co zapisać
Rekord decyzji; hash presetu `legacy`; lista wartości wyniesionych z Kotlina.
